// tests/TestPlacementToCombat.cpp
#include <algorithm>
#include <string>

#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"

#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/GameStateManager.h"
#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/assets/DevAssetStore.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/state/CombatState.h"
#include "game/state/PlacementState.h"
#include "game/state/scripted/ScriptedState.h"

namespace {
glm::vec3 gridToWorld(const GameConfigData& cfg, int col, int row) {
    const float boardOriginX = -((cfg.cols * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    const float boardOriginZ = -((cfg.rows * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    return { boardOriginX + col * cfg.cellSize, 0.0f, boardOriginZ + row * cfg.cellSize };
}

PokemonInstance makeUnit(const GameConfigData& cfg,
                         const std::string& name,
                         PokemonSide side,
                         int col,
                         int row) {
    PokemonInstance unit;
    unit.id = PokemonInstance::getNextUnitID();
    unit.name = name;
    unit.side = side;
    unit.alive = true;
    unit.position = gridToWorld(cfg, col, row);
    unit.hp = 100;
    unit.maxHP = 100;
    unit.attack = 10;
    unit.movementSpeed = 1.0f;
    return unit;
}
} // namespace

bool test_placement_to_combat_headless(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);

    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(77u);
    engine::ManualTimeSource time;

    GameServices services(cfg, db, log, events, assets, rng, time);
    GameWorld world(cfg);
    world.setLogger(&log);
    world.setData(&db);
    world.setRenderEnabled(false);

    const std::string pokemonPath = engine::paths::data("config/pokemon_config.json");
    if (!db.pokemon.loadConfig(pokemonPath, &log, &assets)) {
        outFail = "Failed to load pokemon config: " + pokemonPath;
        return false;
    }
    db.pokemon.applyBaseExpConfig(engine::paths::data("config/pokemon_base_exp.json"), &log, &assets);

    GameStateManager manager;
    const std::string starterName = "bulbasaur";

    PokemonInstance starter;
    starter.name = starterName;
    starter.side = PokemonSide::Player;
    starter.alive = true;
    world.getBenchPokemons().push_back(starter);

    manager.pushState(std::make_unique<PlacementState>(&manager, &world, services, starterName));

    constexpr float dt = 0.25f;
    constexpr int steps = 30; // 7.5 seconds total (placement timer starts at 5.0s)
    for (int i = 0; i < steps; ++i) {
        manager.update(dt);
    }

    GameState* current = manager.getCurrentState();
    if (!current) {
        outFail = "No active state after placement update.";
        return false;
    }
    if (!dynamic_cast<CombatState*>(current)) {
        outFail = "Did not transition to CombatState.";
        return false;
    }

    const auto* starterOnBoard = world.getPokemonByName(starterName);
    if (!starterOnBoard) {
        outFail = "Starter was not placed on the board.";
        return false;
    }

    const auto& bench = world.getBenchPokemons();
    const bool stillOnBench = std::any_of(bench.begin(), bench.end(),
        [&](const PokemonInstance& p) { return p.name == starterName; });
    if (stillOnBench) {
        outFail = "Starter still on bench after placement.";
        return false;
    }

    return true;
}

bool test_combat_route_finishes_headless(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);

    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(91u);
    engine::ManualTimeSource time;

    GameServices services(cfg, db, log, events, assets, rng, time);
    GameWorld world(cfg);
    world.setLogger(&log);
    world.setData(&db);
    world.setRenderEnabled(false);

    world.getPokemons().push_back(makeUnit(cfg, "bulbasaur", PokemonSide::Player, 3, 4));

    const int moneyBefore = world.getMoney();
    const int expectedInterest = std::min(
        std::max(0, cfg.classicInterestCap),
        (std::max(0, moneyBefore) / 10) * std::max(0, cfg.classicInterestPer10));
    const int expectedIncome = std::max(0, cfg.classicBaseIncome) + expectedInterest;

    GameStateManager manager;
    manager.pushState(std::make_unique<CombatState>(
        &manager,
        &world,
        services,
        "scripts/states/route3.lua",
        true));

    manager.update(3.1f);

    GameState* current = manager.getCurrentState();
    auto* scripted = dynamic_cast<ScriptedState*>(current);
    if (!scripted) {
        outFail = "Combat route did not transition to scripted shop state.";
        return false;
    }
    if (scripted->debugScriptPath() != "scripts/states/route3_shop.lua") {
        outFail = "Combat route transitioned to unexpected script: " + scripted->debugScriptPath();
        return false;
    }
    if (world.getMoney() != moneyBefore + expectedIncome) {
        outFail = "Classic route income was not awarded during native route finish.";
        return false;
    }
    if (world.getClassicShopCards().empty()) {
        outFail = "Shop state did not populate classic shop cards after native route finish.";
        return false;
    }

    bool sawRoundClear = false;
    for (const auto& evt : events.peek()) {
        if (evt.type == "Round" && evt.hasPayload && evt.payload == "Round cleared!") {
            sawRoundClear = true;
            break;
        }
    }
    if (!sawRoundClear) {
        outFail = "Native route finish did not emit round clear event.";
        return false;
    }

    return true;
}
