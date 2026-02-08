// tests/TestCombatSlice.cpp
#include <string>

#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"

#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/assets/DevAssetStore.h"
#include "game/config/GameDataDb.h"
#include "game/config/MovesConfigLoader.h"
#include "game/logging/LogBus.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/scripting/LuaBindings_Internal.h"
#include "game/systems/CombatSystem.h"

namespace {
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
    u.hp = 100;
    u.maxHP = 100;
    u.attack = 10;
    u.movementSpeed = 1.0f;
    u.fastMove = fastMove;
    u.chargedMove.clear();
    u.energy = 0;
    u.maxEnergy = 100;
    u.isMoving = false;
    u.moveT = 1.0f;
    u.committedDest = {-1, -1};
    return u;
}
} // namespace

bool test_combat_slice_headless(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);

    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(99u);
    engine::ManualTimeSource time;

    const std::string movesPath = engine::paths::data("config/moves_config.json");
    if (!db.moves.loadConfig(movesPath, nullptr)) {
        outFail = "Failed to load moves config: " + movesPath;
        return false;
    }

    GameServices services(cfg, db, log, events, assets, rng, time);
    GameWorld world(cfg);
    world.setData(&db);
    world.setLogger(&log);

    auto& units = world.getPokemons();
    units.push_back(makeUnit(cfg, "unit_a", PokemonSide::Player, 3, 3, "tackle"));
    units.push_back(makeUnit(cfg, "unit_b", PokemonSide::Enemy, 4, 3, "tackle"));

    const int hpA0 = units[0].hp;
    const int hpB0 = units[1].hp;

    CombatSystem combat(&world, services);

    constexpr float dt = 0.25f;
    constexpr int steps = 80; // 20 seconds total
    for (int i = 0; i < steps; ++i) {
        combat.update(dt);
    }

    const int hpA1 = units[0].hp;
    const int hpB1 = units[1].hp;

    if (hpA1 >= hpA0 && hpB1 >= hpB0) {
        outFail = "Combat slice produced no damage.";
        return false;
    }
    if (hpA1 < 0 || hpB1 < 0) {
        outFail = "Combat slice produced negative HP.";
        return false;
    }

    return true;
}
