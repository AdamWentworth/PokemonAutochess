#include "game/state/ArenaTravelState.h"
#include "game/GameServices.h"
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
constexpr float kRecall = .72f, kCover = .22f, kReveal = .25f, kSend = .72f;
using game::presentation::travelEase;
}

ArenaTravelState::ArenaTravelState(GameWorld& world, GameServices& services, std::string source)
    : world_(world), services_(services), currentScript_(std::move(source)) {}
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
    if (!game::runtime::arena_scene_activation::applyGameplay(services_.assets,
            game::runtime::route1_scene_variants::fromStateScriptPath(currentScript_), world_, &error))
        throw std::runtime_error("Travel source arena: " + error);
    // The prototype knows its destination when the scenario opens. Prepare it
    // before Play; production round flow can make the same request during planning.
    if (services_.prepareArenaScene)
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
    destinationScript_ = currentScript_ == kClearing ? kEntrance : kClearing;
    error_.clear();
    relocated_ = 0;
    world_.setBoardInteractionLocked(true);
    world_.teamTravelVisuals().active = true;
    started_ = true;
    enter(Phase::Hold);
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
    case Phase::Reveal: if (elapsed_ >= kReveal) enter(Phase::SendOut); break;
    case Phase::SendOut: if (elapsed_ >= kSend) enter(Phase::Ready); break;
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
        const float cell = world_.getBoardCellSize();
        visual.ballPosition = world_.conformPositionToGround(unit->position + glm::vec3(0,0,cell*.38f));
        visual.ballPosition.y += cell*.23f;
        const float stagger = formation_.size() > 1 ? .12f * i / (formation_.size()-1) : 0;
        const float progress = std::clamp((elapsed_-stagger)/.6f, 0.0f, 1.0f);
        if (phase_ == Phase::Recall || sending) {
            const float materialize = travelEase((progress-.18f)/.62f);
            visual.scale = sending ? materialize : 1-materialize;
            visual.tint = std::sin(materialize*3.14159265f);
            visual.light = std::sin(materialize*3.14159265f);
            visual.ballScale = world_.getConfig().captureBallScale * (sending ? 1-travelEase((progress-.8f)/.2f) : travelEase(progress/.15f));
            // Open toward the unit, hold during materialization, then close.
            // The full source clip opens past 100 degrees; a smaller opening
            // keeps the shell readable from the overhead board camera.
            visual.ballClip = .3f * travelEase(progress/.18f) * (1-travelEase((progress-.78f)/.22f));
        } else if (phase_ != Phase::Hold) {
            visual.scale = 0;
            visual.ballScale = world_.getConfig().captureBallScale;
        }
        visuals.units.push_back(visual);
    }
}

void ArenaTravelState::handleInput(const InputEvent& event) {
    if (event.type == InputEvent::Type::KeyDown && !event.repeat && event.keyId == InputEvent::Key::R &&
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
