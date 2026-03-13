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
#include "game/ecs/CombatActive.h"
#include "game/ecs/RoundState.h"
#include "game/logging/LogBus.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/state/CombatState.h"
#include "game/state/PlacementState.h"
#include "game/systems/CombatSystem.h"
#include "game/systems/LegacySystemAdapters.h"
#include "game/systems/MovementSystem.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/ShopSystem.h"
#include "game/ui/UIViewport.h"

namespace {
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

