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
