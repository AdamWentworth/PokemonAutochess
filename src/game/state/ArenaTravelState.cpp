#include "game/state/ArenaTravelState.h"
#include "game/GameServices.h"
#include "game/GameStateManager.h"
#include "game/state/scripted/ScriptedState.h"
#include "game/GameWorld.h"
#include "game/PhaseState.h"
#include "game/runtime/shared/scene/ArenaSceneActivation.h"
#include "game/runtime/shared/scene/AuthoredArenaBundle.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"
#include "game/runtime/shared/scene/BoardLayoutDocument.h"
#include "game/runtime/ui/DebugText.h"
#include "game/ui/UIViewport.h"
#include "engine/core/ecs/World.h"
#include "engine/input/InputEvent.h"
#include "engine/render/IRenderBackend.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <chrono>
#include <stdexcept>

namespace {
constexpr const char* kEntrance = "scripts/states/route1_pilot.lua";
constexpr const char* kClearing = "scripts/states/route1_south_clearing.lua";
constexpr float kStagger = .18f;
constexpr float kRecall = 1.0f + kStagger, kCover = .22f, kReveal = .25f;
constexpr float kThrow = .6f + kStagger, kSend = .72f + kStagger;
constexpr float kBallRestPitch = -40.0f;
using game::presentation::travelEase;
}

ArenaTravelState::ArenaTravelState(GameWorld& world, GameServices& services, std::string source)
    : world_(world), services_(services), currentScript_(std::move(source)) {}
ArenaTravelState::ArenaTravelState(GameWorld& world, GameServices& services, std::string source,
                                 GameStateManager& manager, std::string nextShop)
    : world_(world), services_(services), manager_(&manager), nextShopScript_(std::move(nextShop)),
      currentScript_(std::move(source)) {}
ArenaTravelState::~ArenaTravelState() { onExit(); }

void ArenaTravelState::planningFlags() {
    if (services_.ecsWorld && services_.ecsWorld->alive(services_.combatStateEntity)) {
        auto& ecs = *services_.ecsWorld;
        if (auto* combat = ecs.get<game::CombatActive>(services_.combatStateEntity)) combat->active = false;
        if (auto* round = ecs.get<game::RoundState>(services_.combatStateEntity)) round->phase = RoundPhase::Planning;
    }
}

void ArenaTravelState::onEnter() {
    exited_ = false;
    services_.presentationPausesRounds = true;
    planningFlags();
    std::string error;
    if (!manager_ && !game::runtime::arena_scene_activation::applyGameplay(services_.assets,
            game::runtime::route1_scene_variants::fromStateScriptPath(currentScript_), world_, &error))
        throw std::runtime_error("Travel source arena: " + error);
    // The prototype knows its destination when the scenario opens. Prepare it
    // before Play; production round flow can make the same request during planning.
    if (!manager_ && services_.prepareArenaScene)
        services_.prepareArenaScene(currentScript_ == kClearing ? kEntrance : kClearing, error);
    // Capture after snapshot application / stopped-editor placement, on the first Play tick.
    phase_ = Phase::Hold;
    elapsed_ = 0;
    started_ = false;
}

void ArenaTravelState::onExit() {
    if (exited_) return;
    exited_ = true;
    world_.teamTravelVisuals() = {};
    world_.setBoardInteractionLocked(false);
    services_.presentationPausesRounds = false;
    if (services_.discardPreparedArenaScene) services_.discardPreparedArenaScene();
}

void ArenaTravelState::begin() {
    if (world_.countActiveCaptureAttempts() != 0) {
        error_ = "Finish the active capture before travelling.";
        started_ = true;
        enter(Phase::Failed);
        return;
    }
    captureFormation();
    destinationScript_ = manager_ ? currentScript_ : (currentScript_ == kClearing ? kEntrance : kClearing);
    error_.clear();
    relocated_ = 0;
    world_.setBoardInteractionLocked(true);
    world_.teamTravelVisuals().active = true;
    started_ = true;
    enter(Phase::Hold);
}

void ArenaTravelState::captureFormation() {
    formation_.clear();
    auto capture = [&](const auto& units, bool bench) {
        for (const auto& unit : units) {
            if (unit.side != PokemonSide::Player) continue;
            const auto cell = world_.worldToGrid(unit.position);
            formation_.push_back({unit.id, bench, bench ? world_.travelBenchSlot(unit.position) : -1,
                                  {cell.x, cell.y}, unit.rotation});
        }
    };
    capture(world_.getPokemons(), false);
    capture(world_.getBenchPokemons(), true);
}

void ArenaTravelState::enter(Phase phase) {
    phase_ = phase;
    elapsed_ = 0;
    std::clog << "[ArenaTravel] Phase=" << static_cast<int>(phase) << " arena=" << currentScript_ << '\n';
    if (phase == Phase::Load) coverPresented_ = false;
    if (phase == Phase::Warm) warmFrames_ = 0;
    if (phase == Phase::Ready || phase == Phase::Failed) {
        world_.teamTravelVisuals() = {};
        world_.setBoardInteractionLocked(false);
    }
}

float ArenaTravelState::coverAlpha() const {
    if (phase_ == Phase::Cover) return travelEase(elapsed_ / kCover);
    if (phase_ == Phase::Load || phase_ == Phase::Warm) return 1;
    if (phase_ == Phase::Reveal) return 1 - travelEase(elapsed_ / kReveal);
    return 0;
}

void ArenaTravelState::worldFramePresented(bool ready) {
    if (phase_ == Phase::Warm && ready) ++warmFrames_;
}

bool ArenaTravelState::loadDestination() {
    const auto started = std::chrono::steady_clock::now();
    if (manager_) {
        // The active flat arena already has the right geometry, rules, shadows
        // and GPU resources. Do not unpack/rebuild it at every shop or round.
        world_.restorePlayerPositionsAfterBattle();
        world_.healPlayerUnitsToFull();
        auto& units = world_.getPokemons();
        std::erase_if(units, [](const auto& unit) { return unit.side == PokemonSide::Enemy; });
        captureFormation();
        std::clog << "[ArenaTravel] Round formation restored in retained arena=" << currentScript_
                  << " units=" << formation_.size() << " prepare_ms="
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-started).count() << '\n';
        return true;
    }
    using namespace game::runtime;
    const auto& variant = route1_scene_variants::fromStateScriptPath(destinationScript_);
    authored_arena::Bundle bundle;
    route1_environment::BoardLayoutTransform layout;
    if (!bundle.load(services_.assets, std::string(variant.arenaBundlePath), &error_) ||
        !route1_environment::loadBoardLayoutTransform(bundle.store, bundle.boardPath, layout, &error_)) return false;
    const int cols = world_.getConfig().cols, rows = world_.getConfig().rows;
    auto valid = [&](game::arena::Cell cell) {
        return cell.x >= 0 && cell.x < cols && cell.z >= 0 && cell.z < rows &&
            bundle.map.playableCells.contains({cell.x + layout.terrainGridOrigin[0], cell.z + layout.terrainGridOrigin[1]});
    };
    auto placed = formation_;
    std::set<std::pair<int, int>> occupied;
    std::vector<std::size_t> displaced;
    // Reserve all valid original slots before assigning any fallback.
    for (std::size_t i = 0; i < placed.size(); ++i) {
        const auto& slot = placed[i];
        if (slot.bench) continue;
        if (!valid(slot.cell) || !occupied.insert({slot.cell.x, slot.cell.z}).second) displaced.push_back(i);
    }
    for (auto index : displaced) {
        auto& slot = placed[index];
        game::arena::Cell best;
        int distance = std::numeric_limits<int>::max();
        for (int z = 0; z < rows; ++z) for (int x = 0; x < cols; ++x) {
            if (!valid({x,z}) || occupied.contains({x,z})) continue;
            const int d = std::abs(x-slot.cell.x) + std::abs(z-slot.cell.z);
            if (d < distance) { distance = d; best = {x,z}; }
        }
        if (best.x < 0) { error_ = "The destination has no free legal cell for the whole formation."; return false; }
        slot.cell = best;
        occupied.insert({best.x,best.z});
    }
    if (services_.prepareArenaScene && !services_.prepareArenaScene(destinationScript_, error_)) return false;
    if (!arena_scene_activation::applyGameplay(services_.assets, variant, world_, &error_)) return false;
    for (const auto& slot : placed) {
        if (auto* unit = world_.findUnitById(slot.id)) {
            unit->position = slot.bench ? world_.travelBenchPosition(slot.benchSlot) : world_.gridToWorld(slot.cell.x, slot.cell.z);
            unit->position = world_.conformPositionToGround(unit->position);
            unit->rotation = slot.rotation;
            unit->isMoving = false;
            unit->moveFrom = unit->moveTo = unit->position;
            unit->moveT = 1;
            unit->committedDest = {-1,-1};
            unit->ledgeJump = {};
            unit->targetMemory = {};
            unit->patrol = {};
            unit->coverRevealRemainingSec = 0;
        }
    }
    relocated_ = static_cast<int>(displaced.size());
    formation_ = std::move(placed);
    currentScript_ = destinationScript_;
    std::clog << "[ArenaTravel] Destination prepared: " << variant.sceneId
              << " units=" << formation_.size() << " relocated=" << relocated_
              << " prepare_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-started).count() << '\n';
    return true;
}

void ArenaTravelState::update(float dt) {
    if (!std::isfinite(dt) || dt <= 0) return;
    planningFlags();
    if (!started_) begin();
    elapsed_ += std::min(dt, .1f);
    switch (phase_) {
    case Phase::Hold: if (elapsed_ >= .65f) enter(Phase::Recall); break;
    case Phase::Recall: if (elapsed_ >= kRecall) enter(Phase::Cover); break;
    case Phase::Cover: if (elapsed_ >= kCover) enter(Phase::Load); break;
    case Phase::Load:
        if (coverPresented_ || !services_.renderEnabled) {
            if (loadDestination()) enter(Phase::Warm);
            else {
                std::cerr << "[ArenaTravel] " << error_ << '\n';
                if (services_.discardPreparedArenaScene) services_.discardPreparedArenaScene();
                enter(Phase::Failed);
            }
        }
        break;
    case Phase::Warm:
        if ((warmFrames_ >= 2 || !services_.renderEnabled) && elapsed_ >= .12f) enter(Phase::Reveal);
        break;
    case Phase::Reveal: if (elapsed_ >= kReveal) enter(Phase::Throw); break;
    case Phase::Throw: if (elapsed_ >= kThrow) enter(Phase::SendOut); break;
    case Phase::SendOut: if (elapsed_ >= kSend) enter(Phase::Ready); break;
    case Phase::Ready:
        if (manager_) {
            manager_->popState();
            manager_->pushState(std::make_unique<ScriptedState>(manager_, &world_, services_, nextShopScript_, currentScript_));
        }
        break;
    default: break;
    }
    updateVisuals();
}

void ArenaTravelState::updateVisuals() {
    auto& visuals = world_.teamTravelVisuals();
    visuals.units.clear();
    if (!visuals.active) return;
    const bool sending = phase_ == Phase::SendOut;
    for (std::size_t i = 0; i < formation_.size(); ++i) {
        const auto* unit = world_.findUnitById(formation_[i].id);
        if (!unit) continue;
        game::presentation::TravelUnitVisual visual;
        visual.id = unit->id;
        visual.sendingOut = sending;
        // Expose the white hemisphere/band to the overhead camera.
        visual.ballPitchDeg = kBallRestPitch;
        const float cell = world_.getBoardCellSize();
        const float ballScale = world_.getConfig().captureBallScale * 1.4f;
        const float stagger = formation_.size() > 1 ? kStagger * i / (formation_.size()-1) : 0;
        const float time = std::max(0.0f, elapsed_-stagger);
        // Keep recall balls visibly separate from the body, even for bench units.
        glm::vec3 recallBall = world_.conformPositionToGround(unit->position + glm::vec3(0,0,cell*1.5f));
        recallBall.y = std::max(recallBall.y, unit->position.y) + cell*.65f;
        glm::vec3 landing = world_.conformPositionToGround(unit->position + glm::vec3(0,0,cell*.45f));
        landing.y += cell*.24f;
        if (phase_ == Phase::Recall) {
            const float absorb = travelEase((time-.3f)/.5f);
            visual.ballPosition = recallBall;
            visual.ballScale = ballScale * travelEase(time/.12f);
            // Recall uses a closed ball and its beam; opening belongs to send-out.
            visual.light = travelEase((time-.12f)/.12f) * (1-travelEase((time-.78f)/.12f));
            visual.tint = travelEase((time-.2f)/.12f);
            visual.scale = 1-absorb;
            visual.unitOffset = (recallBall-unit->position) * absorb;
        } else if (phase_ == Phase::Throw) {
            const float progress = std::clamp(time/.6f, 0.0f, 1.0f);
            glm::vec3 launch = world_.travelBenchPosition(0);
            launch.x = unit->position.x * .4f;
            launch.z += cell*1.8f;
            launch = world_.conformPositionToGround(launch);
            launch.y += cell*.9f;
            const float distance = glm::length(glm::vec2(landing.x-launch.x, landing.z-launch.z));
            const float arcHeight = std::clamp(distance*.3f, cell*1.3f, cell*2.6f);
            visual.ballPosition = glm::mix(launch, landing, progress);
            visual.ballPosition.y += 4*progress*(1-progress)*arcHeight;
            visual.ballScale = ballScale * travelEase(time/.06f);
            visual.ballPitchDeg = kBallRestPitch + 360*progress;
            visual.scale = 0;
        } else if (sending) {
            const float materialize = travelEase((time-.14f)/.4f);
            visual.ballPosition = landing;
            visual.ballScale = ballScale * (1-travelEase((time-.56f)/.16f));
            visual.ballClip = .3f * travelEase(time/.12f) * (1-travelEase((time-.5f)/.12f));
            visual.light = travelEase((time-.07f)/.1f) * (1-travelEase((time-.48f)/.12f));
            visual.tint = 1-materialize;
            visual.scale = materialize;
            visual.unitOffset = (landing-unit->position) * (1-materialize);
        } else if (phase_ == Phase::Cover) {
            visual.ballPosition = recallBall;
            visual.scale = 0;
            visual.ballScale = ballScale;
        } else if (phase_ != Phase::Hold) {
            // Reveal the empty destination before any balls are thrown onto it.
            visual.scale = 0;
        }
        visuals.units.push_back(visual);
    }
}

void ArenaTravelState::handleInput(const InputEvent& event) {
    if (!manager_ && event.type == InputEvent::Type::KeyDown && !event.repeat && event.keyId == InputEvent::Key::R &&
        (phase_ == Phase::Ready || phase_ == Phase::Failed)) begin();
}

void ArenaTravelState::render() {
    if (!services_.renderer) return;
    const int width = services_.viewport ? services_.viewport->width : 1280;
    const int height = services_.viewport ? services_.viewport->height : 720;
    const float cover = coverAlpha();
    if (cover > 0) {
        IRenderBackend::DebugQuad quad;
        quad.w = static_cast<float>(width); quad.h = static_cast<float>(height);
        quad.r = .025f; quad.g = .035f; quad.b = .04f; quad.a = cover;
        services_.renderer->drawDebugQuads(&quad, 1, width, height);
        if (phase_ == Phase::Load) coverPresented_ = true;
    }
    if (cover >= 1 || (phase_ != Phase::Ready && phase_ != Phase::Failed && phase_ != Phase::Hold)) return;
    if (manager_ && phase_ != Phase::Failed) return;
    std::string label = currentScript_ == kClearing ? "SOUTH CLEARING" : "SOUTH ENTRANCE";
    if (phase_ == Phase::Ready) label += "  -  Formation restored. R: travel back";
    else if (phase_ == Phase::Failed) label = "Travel failed; team preserved. R: retry";
    else label += "  -  Preparing to travel";
    if (relocated_ > 0) label += " (" + std::to_string(relocated_) + " moved to free cells)";
    std::vector<IRenderBackend::DebugLine> text;
    const float scale = std::clamp(width/1000.0f, .75f, 1.4f);
    game::runtime::ui_text::appendTextLines(text, 20, 22, label, scale, 1, .96f, .85f, 1, .88f);
    services_.renderer->drawDebugLines(text.data(), text.size(), width, height);
}
