// tests/TestEndToEnd.cpp
#include <string>

#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "engine/core/Services.h"
#include "engine/core/TimeSources.h"
#include "engine/core/ecs/Scheduler.h"
#include "engine/core/ecs/World.h"

#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/GameStateManager.h"
#include "game/runtime/session/GameUpdateGraph.h"
#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/assets/DevAssetStore.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/GameDataDb.h"
#include "game/config/MovesConfigLoader.h"
#include "game/PhaseState.h"
#include "game/logging/LogBus.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/state/CombatState.h"
#include "game/state/ArenaTravelState.h"
#include "game/runtime/shared/capture/SharedCapturePresentation.h"
#include "game/state/PlacementState.h"
#include "game/state/scripted/ScriptedState.h"
#include "engine/render/IRenderBackend.h"
#include "game/systems/CombatSystem.h"
#include "game/systems/LegacySystemAdapters.h"
#include "game/systems/MovementSystem.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/ShopSystem.h"
#include "game/ui/UIViewport.h"

namespace {
class StarterRecordingBackend final : public IRenderBackend {
  public:
    const char *backendId() const override { return "test"; }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int, int) override {}
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return false; }
    void shutdown() override {}
    void drawDebugSprites(const DebugSprite *values, std::size_t count, int, int) override {
        sprites.insert(sprites.end(), values, values + count);
    }
    void drawDebugQuads(const DebugQuad *values, std::size_t count, int, int) override {
        quads.insert(quads.end(), values, values + count);
    }
    void drawDebugLines(const DebugLine *values, std::size_t count, int, int) override {
        lines.insert(lines.end(), values, values + count);
    }
    void clear() { sprites.clear(); quads.clear(); lines.clear(); }
    std::vector<DebugSprite> sprites;
    std::vector<DebugQuad> quads;
    std::vector<DebugLine> lines;
};

glm::vec3 gridToWorld(const GameConfigData& cfg, int col, int row) {
    float boardOriginX = -((cfg.cols * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    float boardOriginZ = -((cfg.rows * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    return { boardOriginX + col * cfg.cellSize, 0.0f, boardOriginZ + row * cfg.cellSize };
}

PokemonInstance makeUnit(const GameConfigData& cfg,
                         const std::string& name,
                         PokemonSide side,
                         int col,
                         int row,
                         const std::string& fastMove) {
    PokemonInstance u;
    u.id = PokemonInstance::getNextUnitID();
    u.name = name;
    u.side = side;
    u.alive = true;
    u.position = gridToWorld(cfg, col, row);
    u.baseHp = 100;
    u.baseAttack = 10;
    u.baseMovementSpeed = 1.0f;
    u.hp = 100;
    u.maxHP = 100;
    u.attack = 10;
    u.movementSpeed = 1.0f;
    u.fastMove = fastMove;
    u.chargedMove.clear();
    u.energy = 0;
    u.maxEnergy = 100;
    return u;
}
} // namespace

bool test_arena_travel_contract(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(712u);
    engine::ManualTimeSource time;
    StarterRecordingBackend renderer;
    game::ui::UIViewport viewport;
    viewport.set(844, 512);
    GameServices services(cfg, db, log, events, assets, rng, time, nullptr, {}, &viewport, true);
    services.renderer = &renderer;
    GameWorld world(cfg);
    auto first = makeUnit(cfg, "bulbasaur", PokemonSide::Player, 1, 5, "tackle");
    first.level = 5; first.hp = 67; first.energy = 23; first.xp = 19;
    first.rotation.y = 137;
    auto second = makeUnit(cfg, "squirtle", PokemonSide::Player, 6, 6, "tackle");
    auto bench = makeUnit(cfg, "pidgey", PokemonSide::Player, 0, 0, "tackle");
    bench.position = world.travelBenchPosition(4);
    world.getPokemons() = {first, second};
    world.getBenchPokemons() = {bench};
    int prepared = 0;
    bool failLoad = false;
    services.prepareArenaScene = [&](const std::string&, std::string& error) {
        ++prepared;
        if (failLoad) error = "Injected load failure";
        return !failLoad;
    };
    ArenaTravelState travel(world, services, "scripts/states/route1_pilot.lua");
    travel.onEnter();
    const auto sourcePosition = world.findUnitById(first.id)->position;
    auto tick = [&]() { travel.update(1.0f/60); world.update(1.0f/60); };
    for (int i = 0; i < 70; ++i) tick();
    game::runtime::shared_capture::SnapshotCache balls;
    if (!balls.refresh(&world) || balls.snaps.size() != 3 || world.countActiveCaptureAttempts() != 0 ||
        !world.isBoardInteractionLocked() || !services.presentationPausesRounds) {
        outFail = "recall must render all team balls without capture attempts and lock gameplay"; return false;
    }
    const auto* recalling = world.teamTravelVisuals().find(first.id);
    if (!recalling || recalling->light < .9f || recalling->ballScale <= 0 ||
        glm::length(recalling->ballPosition - sourcePosition) < world.getBoardCellSize() ||
        recalling->unitOffset == glm::vec3(0) || world.findUnitById(first.id)->position != sourcePosition) {
        outFail = "recall must connect a separate visible ball to the body and draw the body toward it without moving the gameplay unit"; return false;
    }
    for (int i = 0; i < 200; ++i) tick();
    if (prepared != 1 || travel.phase() != ArenaTravelState::Phase::Load || travel.coverAlpha() != 1 ||
        travel.debugScriptPath() != "scripts/states/route1_pilot.lua") {
        outFail = "prefetched destination cannot activate before a fully covered frame has actually been drawn"; return false;
    }
    travel.render();
    tick();
    for (int i = 0; i < 90; ++i) tick();
    if (prepared != 2 || travel.phase() != ArenaTravelState::Phase::Warm || travel.coverAlpha() != 1) {
        outFail = "destination must remain covered until world rendering confirms it is ready"; return false;
    }
    travel.worldFramePresented(true);
    tick();
    if (travel.phase() != ArenaTravelState::Phase::Warm) {
        outFail = "one destination draw must not unlock travel"; return false;
    }
    travel.worldFramePresented(true);
    for (int i = 0; i < 60 && travel.phase() != ArenaTravelState::Phase::Throw; ++i) tick();
    if (travel.phase() != ArenaTravelState::Phase::Throw) {
        outFail = "the destination reveal must lead to a ball throw before send-out"; return false;
    }
    const auto launch = world.teamTravelVisuals().find(first.id)->ballPosition;
    const auto destination = world.findUnitById(first.id)->position;
    for (int i = 0; i < 18; ++i) tick();
    const auto airborne = *world.teamTravelVisuals().find(first.id);
    if (airborne.scale != 0 || airborne.light != 0 || airborne.ballClip != 0 || airborne.ballScale <= 0 ||
        airborne.ballPitchDeg <= 0 || world.findUnitById(first.id)->position != destination ||
        !balls.refresh(&world) || balls.findByTarget(first.id)->presentationPitchDeg != airborne.ballPitchDeg) {
        outFail = "arrival balls must travel closed and spinning while Pokemon remain hidden and their cells stay fixed"; return false;
    }
    for (int i = 0; i < 60 && travel.phase() == ArenaTravelState::Phase::Throw; ++i) tick();
    const auto landing = world.teamTravelVisuals().find(first.id)->ballPosition;
    if (travel.phase() != ArenaTravelState::Phase::SendOut || glm::length(landing-launch) < world.getBoardCellSize() ||
        airborne.ballPosition.y <= std::max(launch.y, landing.y) ||
        glm::length(glm::vec2(landing.x-destination.x, landing.z-destination.z)) > world.getBoardCellSize()) {
        outFail = "ball throws must arc above both endpoints and arrive at their assigned slot before opening"; return false;
    }
    for (int i = 0; i < 100; ++i) tick();
    const auto* arrived = world.findUnitById(first.id);
    const auto* reserve = world.findUnitById(bench.id);
    if (travel.phase() != ArenaTravelState::Phase::Ready || world.isBoardInteractionLocked() ||
        !world.teamTravelVisuals().units.empty() || !arrived || !reserve ||
        arrived->hp != 67 || arrived->energy != 23 || arrived->xp != 19 || arrived->level != 5 ||
        arrived->rotation.y != 137 || world.worldToGrid(arrived->position) != glm::ivec2(1,5) ||
        world.travelBenchSlot(reserve->position) != 4 || world.getPokemons().size() != 2 ||
        world.getBenchPokemons().size() != 1 || arrived->captureInProgress) {
        outFail = "arrival must retain identities, stats, facing, board cells and bench slots without duplicates"; return false;
    }
    const auto position = arrived->position;
    failLoad = true;
    travel.handleInput(InputEvent::KeyDownEvent(InputEvent::Key::R));
    for (int i = 0; i < 200; ++i) { tick(); travel.render(); }
    if (travel.phase() != ArenaTravelState::Phase::Failed || world.findUnitById(first.id)->position != position ||
        world.teamTravelVisuals().active || world.isBoardInteractionLocked() ||
        travel.debugScriptPath() != "scripts/states/route1_south_clearing.lua") {
        outFail = "failed scene preparation must leave the team visible and unchanged in the original arena"; return false;
    }
    travel.onExit();
    if (services.presentationPausesRounds) { outFail = "leaving travel must release its round pause"; return false; }
    // Overlapping source placements get distinct destinations without moving the valid first slot.
    failLoad = false;
    world.findUnitById(second.id)->position = world.findUnitById(first.id)->position;
    travel.onEnter();
    for (int i = 0; i < 360; ++i) { tick(); travel.render(); travel.worldFramePresented(true); }
    if (travel.phase() != ArenaTravelState::Phase::Ready ||
        world.worldToGrid(world.findUnitById(first.id)->position) != glm::ivec2(1,5) ||
        world.worldToGrid(world.findUnitById(second.id)->position) == glm::ivec2(1,5)) {
        outFail = "fallback placement must reserve valid cells first and resolve overlaps deterministically"; return false;
    }
    engine::ecs::World ecs;
    auto phaseEntity = ecs.create();
    RoundSystem rounds(services, phaseEntity);
    rounds.debugSetPhase(RoundPhase::Planning, .1f);
    rounds.update(ecs, .2f);
    if (rounds.getCurrentPhase() != RoundPhase::Planning) {
        outFail = "travel preview must not consume the planning clock"; return false;
    }
    travel.onExit();
    rounds.update(ecs, .2f);
    if (rounds.getCurrentPhase() != RoundPhase::Battle) {
        outFail = "round timing must resume when the travel preview exits"; return false;
    }
    return true;
}

bool test_starter_frontend_selection_contract(std::string &outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(1337u);
    engine::ManualTimeSource time;
    if (!db.moves.loadConfig(engine::paths::data("config/moves_config.json"), nullptr) ||
        !db.pokemon.loadConfig(engine::paths::data("config/pokemon_config.json"), &log, &assets)) {
        outFail = "starter fixture data did not load";
        return false;
    }
    const std::string names[] = {"bulbasaur", "charmander", "squirtle"};
    const InputEvent::Key keys[] = {InputEvent::Key::Num1, InputEvent::Key::Num2, InputEvent::Key::Num3};
    for (const std::string mode : {"classic", "adventure"}) {
        for (int choice = 0; choice < 3; ++choice) {
            game::ui::UIViewport viewport;
            viewport.set(1280, 720);
            GameServices services(cfg, db, log, events, assets, rng, time, nullptr, {}, &viewport, true);
            StarterRecordingBackend renderer;
            services.renderer = &renderer;
            services.gameMode = mode;
            GameWorld world(cfg);
            world.setLogger(&log);
            world.setData(&db);
            world.setRenderEnabled(false);
            GameStateManager manager;
            auto starter = std::make_unique<ScriptedState>(&manager, &world, services, "scripts/states/starter.lua");
            auto *state = starter.get();
            manager.pushState(std::move(starter));
            // Reproduce editor embedding: viewport changes without a Resize event.
            viewport.set(844, 512);
            state->render();
            if (state->shouldRenderWorld() || renderer.sprites.size() != 1u ||
                !renderer.quads.empty() || !renderer.lines.empty()) {
                outFail = "starter intro must begin with just the lab, with no title, cards or hint";
                return false;
            }
            const auto initial = renderer.sprites.front();
            const auto earlyInputIsBlocked = [&]() {
                state->handleInput(InputEvent::KeyDownEvent(keys[choice]));
                if (manager.getCurrentState() != state) return false;
                InputEvent click;
                click.type = InputEvent::Type::MouseDown;
                click.mouseButtonId = InputEvent::MouseButton::Left;
                // This is inside the eventual middle card in the embedded view.
                click.mouseX = 422;
                click.mouseY = 420;
                state->handleInput(click);
                return manager.getCurrentState() == state && world.getPokemons().empty() &&
                       world.getBenchPokemons().empty();
            };
            if (!earlyInputIsBlocked()) {
                outFail = "starter input must be ignored during the opening hold";
                return false;
            }
            state->update(.5f);
            renderer.clear();
            state->render();
            if (renderer.sprites.size() != 1u || renderer.sprites.front().u0 != initial.u0) {
                outFail = "the opening hold must leave the full lab framing still";
                return false;
            }
            state->update(1.1f);
            renderer.clear();
            state->render();
            const auto moving = renderer.sprites.front();
            if (renderer.sprites.size() != 1u || moving.texturePath != "assets/ui/backdrops/oaks_lab_intro_7.png" ||
                !renderer.quads.empty() || !renderer.lines.empty() || !earlyInputIsBlocked()) {
                outFail = "camera must move before UI appears, with selection still locked";
                return false;
            }
            // Rendering and embedded resizing must not restart or advance time.
            viewport.set(900, 600);
            renderer.clear();
            state->render();
            viewport.set(844, 512);
            renderer.clear();
            state->render();
            if (renderer.sprites.front().u0 != moving.u0 || renderer.sprites.front().v0 != moving.v0) {
                outFail = "rendering and viewport changes must preserve intro progress";
                return false;
            }
            state->update(1.425f); // Halfway through the fade, after the camera settles.
            renderer.clear();
            state->render();
            if (renderer.sprites.size() != 7u || renderer.quads.empty() || renderer.lines.empty()) {
                outFail = "the settled intro must fade in both UI bands and all card layers";
                return false;
            }
            const auto settled = renderer.sprites.front();
            for (std::size_t i = 1; i < renderer.sprites.size(); ++i) {
                if (std::abs(renderer.sprites[i].a - .5f) > .01f) {
                    outFail = "card artwork and gold frames must fade together";
                    return false;
                }
            }
            if (std::abs(renderer.quads.front().a - .435f) > .01f ||
                std::abs(renderer.lines.front().a - .5f) > .01f || !earlyInputIsBlocked()) {
                outFail = "panels and text must fade with cards, and fading cards cannot be selected";
                return false;
            }
            state->update(.5f);
            renderer.clear();
            state->render();
            if (renderer.sprites.front().u0 != settled.u0 || renderer.sprites.front().v0 != settled.v0 ||
                renderer.sprites[1].a != 1.0f) {
                outFail = "camera must remain settled while choices become fully visible";
                return false;
            }
            if (mode == "classic" && choice == 0) {
                state->onExit();
                state->onEnter();
                renderer.clear();
                state->render();
                if (renderer.sprites.size() != 1u || renderer.sprites.front().u0 != initial.u0 ||
                    !earlyInputIsBlocked()) {
                    outFail = "re-entering starter selection must replay the intro";
                    return false;
                }
                state->update(4.0f);
                renderer.clear();
                state->render();
            }
            if (state->shouldRenderWorld() || renderer.sprites.size() != 7u) {
                outFail = "starter frontend must render only the backdrop and three image/frame pairs";
                return false;
            }
            const auto &backdrop = renderer.sprites[0];
            if (backdrop.texturePath != "assets/ui/backdrops/oaks_lab_table.png" ||
                backdrop.w != 844 || backdrop.h != 512) {
                outFail = "starter backdrop must follow the embedded surface dimensions";
                return false;
            }
            for (std::size_t i = 1; i < renderer.sprites.size(); ++i) {
                const auto &sprite = renderer.sprites[i];
                if (sprite.x < 0 || sprite.y < 0 || sprite.x + sprite.w > 844 || sprite.y + sprite.h > 512) {
                    outFail = "starter cards must fit the editor without a window Resize event";
                    return false;
                }
            }
            const float ballCenters[] = {.330508f, .496782f, .669492f};
            for (int i = 0; i < 3; ++i) {
                const auto& image = renderer.sprites[1 + i * 2];
                const float ballX = backdrop.w * (ballCenters[i] - backdrop.u0) / (backdrop.u1 - backdrop.u0);
                if (std::abs(image.x + image.w * .5f - ballX) > 1.0f) {
                    outFail = "starter script must align the clickable artwork with each displayed Pokeball";
                    return false;
                }
            }
            InputEvent select;
            if (mode == "classic") {
                const auto &image = renderer.sprites[1 + choice * 2];
                select.type = InputEvent::Type::MouseDown;
                select.mouseButtonId = InputEvent::MouseButton::Left;
                select.mouseX = static_cast<int>(image.x + image.w * .5f);
                select.mouseY = static_cast<int>(image.y + image.h * .5f);
            } else {
                select = InputEvent::KeyDownEvent(keys[choice]);
            }
            state->handleInput(select);
            if (!dynamic_cast<PlacementState *>(manager.getCurrentState())) {
                outFail = "starter click/number key must enter placement";
                return false;
            }
            int matching = 0;
            for (const auto &unit : world.getPokemons())
                if (unit.name == names[choice] && unit.level == 5 && unit.side == PokemonSide::Player) ++matching;
            for (const auto &unit : world.getBenchPokemons())
                if (unit.name == names[choice] && unit.level == 5 && unit.side == PokemonSide::Player) ++matching;
            if (matching != 1) {
                outFail = "starter selection must create exactly one chosen level-5 Pokemon";
                return false;
            }
        }
    }
    return true;
}

bool test_end_to_end_headless(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);

    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(1337u);
    engine::ManualTimeSource time;

    const std::string movesPath = engine::paths::data("config/moves_config.json");
    if (!db.moves.loadConfig(movesPath, nullptr)) {
        outFail = "Failed to load moves config: " + movesPath;
        return false;
    }
    const std::string attackPath = engine::paths::data("config/attack_anim_config.json");
    if (!db.attackAnims.loadConfig(attackPath, nullptr)) {
        outFail = "Failed to load attack anim config: " + attackPath;
        return false;
    }
    const std::string pokemonPath = engine::paths::data("config/pokemon_config.json");
    if (!db.pokemon.loadConfig(pokemonPath, &log, &assets)) {
        outFail = "Failed to load pokemon config: " + pokemonPath;
        return false;
    }
    db.pokemon.applyBaseExpConfig(engine::paths::data("config/pokemon_base_exp.json"), &log, &assets);

    engine::CoreServices core;
    core.rng = &rng;
    core.time = &time;

    engine::ecs::World ecsWorld(&core);
    engine::ecs::Entity phaseEntity = ecsWorld.create();
    ecsWorld.add<game::CombatActive>(phaseEntity, game::CombatActive{false});

    game::ui::UIViewport viewport;
    viewport.set(1280, 720);

    GameServices services(cfg, db, log, events, assets, rng, time, &ecsWorld, phaseEntity, &viewport);

    GameWorld world(cfg);
    world.setLogger(&log);
    world.setData(&db);
    world.setRenderEnabled(false);

    GameStateManager manager;

    const std::string starterName = "bulbasaur";
    world.getBenchPokemons().push_back(makeUnit(cfg, starterName, PokemonSide::Player, 3, 0, "tackle"));
    world.getPokemons().push_back(makeUnit(cfg, "rattata", PokemonSide::Enemy, 3, 1, "tackle"));

    engine::ecs::Scheduler scheduler;
    auto shop = std::make_unique<ShopSystem>(services.rng);
    ShopSystem* shopPtr = shop.get();
    scheduler.add(std::move(shop), engine::ecs::Scheduler::Phase::Update);

    auto roundSys = std::make_unique<RoundSystem>(services, phaseEntity);
    RoundSystem* roundPtr = roundSys.get();
    ecsWorld.add<game::RoundState>(phaseEntity, game::RoundState{ roundPtr->getCurrentPhase() });
    scheduler.add(std::move(roundSys), engine::ecs::Scheduler::Phase::Update);

    scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
        [&manager](float dt) { manager.update(dt); }
    ), engine::ecs::Scheduler::Phase::PostUpdate);

    scheduler.add(std::make_unique<MovementSystem>(&world, services, phaseEntity),
                  engine::ecs::Scheduler::Phase::PostUpdate);
    scheduler.add(std::make_unique<CombatSystem>(&world, services, phaseEntity),
                  engine::ecs::Scheduler::Phase::PostUpdate);

    game::GameUpdateGraph graph;
    graph.configure({
        &scheduler,
        &ecsWorld,
        phaseEntity,
        shopPtr,
        &log,
        &events
    });

    manager.pushState(std::make_unique<PlacementState>(&manager, &world, services, starterName));

    bool sawCombatState = false;
    bool sawCombatActiveEnabled = false;
    bool sawBattle = false;
    bool sawResolution = false;
    bool sawDamage = false;
    bool capturedBaseline = false;
    int playerHp0 = 0;
    int enemyHp0 = 0;

    constexpr float dt = 0.25f;
    constexpr int maxSteps = 240; // 60 seconds total

    for (int i = 0; i < maxSteps; ++i) {
        time.advance(dt);
        graph.tick(dt);

        if (auto* current = manager.getCurrentState()) {
            if (dynamic_cast<CombatState*>(current)) {
                sawCombatState = true;
            }
        }

        auto* roundState = ecsWorld.get<game::RoundState>(phaseEntity);
        if (roundState) {
            if (roundState->phase == RoundPhase::Battle) sawBattle = true;
            if (roundState->phase == RoundPhase::Resolution) sawResolution = true;
        }
        if (auto* combatActive = ecsWorld.get<game::CombatActive>(phaseEntity)) {
            if (combatActive->active) sawCombatActiveEnabled = true;
        }

        if (sawCombatState && !capturedBaseline) {
            for (const auto& u : world.getPokemons()) {
                if (u.side == PokemonSide::Player) playerHp0 = u.hp;
                if (u.side == PokemonSide::Enemy) enemyHp0 = u.hp;
            }
            capturedBaseline = (playerHp0 > 0 && enemyHp0 > 0);
        }

        if (capturedBaseline) {
            for (const auto& u : world.getPokemons()) {
                if (u.side == PokemonSide::Player && u.hp < playerHp0) sawDamage = true;
                if (u.side == PokemonSide::Enemy && u.hp < enemyHp0) sawDamage = true;
            }
        }

        if (sawCombatState && sawBattle && sawResolution && sawDamage) {
            break;
        }
    }

    if (!sawCombatState) {
        outFail = "Did not transition to CombatState.";
        return false;
    }

    if (!sawCombatActiveEnabled) {
        outFail = "CombatActive was never enabled during end-to-end run.";
        return false;
    }

    if (!sawBattle) {
        outFail = "Did not reach Battle phase during end-to-end run.";
        return false;
    }
    if (!sawResolution) {
        outFail = "Did not reach Resolution phase during end-to-end run.";
        return false;
    }
    if (!sawDamage) {
        outFail = "Combat did not apply damage during end-to-end run.";
        return false;
    }

    return true;
}

