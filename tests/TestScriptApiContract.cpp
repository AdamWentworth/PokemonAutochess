// tests/TestScriptApiContract.cpp
#include <cmath>
#include <string>
#include <unordered_map>

#include <sol/sol.hpp>

#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"

#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/assets/DevAssetStore.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/scripting/LuaBindings.h"
#include "game/scripting/ScriptAPI.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/scripting/LuaBindings_Internal.h"

namespace {
PokemonInstance makeUnit(const GameConfigData& cfg,
                         const std::string& name,
                         PokemonSide side,
                         int col,
                         int row,
                         float speed = 1.0f) {
    PokemonInstance u;
    u.id = PokemonInstance::getNextUnitID();
    u.name = name;
    u.side = side;
    u.alive = true;
    u.movementSpeed = speed;
    u.position = gridToWorld(cfg, col, row);
    u.hp = 100;
    u.maxHP = 100;
    u.attack = 10;
    u.energy = 0;
    u.maxEnergy = 100;
    u.isMoving = false;
    u.moveT = 1.0f;
    u.committedDest = {-1, -1};
    return u;
}

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (!condition) {
        outFail = message;
        return false;
    }
    return true;
}
} // namespace

bool test_script_api_contract(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);

    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(7u);
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
    PokemonInstance a = makeUnit(cfg, "unit_a", PokemonSide::Player, 1, 1);
    a.fastMove = "tackle";
    a.chargedMove = "seed_bomb";
    units.push_back(a);

    PokemonInstance b = makeUnit(cfg, "unit_b", PokemonSide::Enemy, 2, 1);
    units.push_back(b);

    PokemonInstance c = makeUnit(cfg, "unit_c", PokemonSide::Enemy, 6, 6);
    units.push_back(c);

    ScriptAPI api(&world, nullptr, services);
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
    registerLuaBindings(lua, api);

    // world_list_units should return 3 entries with core fields.
    sol::function listFn = lua["world_list_units"];
    if (!expect(listFn.valid(), "world_list_units binding missing.", outFail)) return false;
    sol::table list = listFn();
    std::unordered_map<int, sol::table> byId;
    for (auto& kv : list) {
        sol::table t = kv.second.as<sol::table>();
        const int id = t["id"];
        byId[id] = t;
    }
    if (!expect(byId.size() == 3, "world_list_units returned unexpected size.", outFail)) return false;

    const int idA = units[0].id;
    if (!expect(byId.count(idA) == 1, "world_list_units missing unit_a.", outFail)) return false;
    sol::table ta = byId[idA];
    const std::string name = ta["name"].get_or(std::string());
    const bool alive = ta["alive"].get_or(false);
    const int col = ta["col"].get_or(-1);
    const int row = ta["row"].get_or(-1);
    if (!expect(name == "unit_a", "world_list_units name mismatch.", outFail)) return false;
    if (!expect(alive == true, "world_list_units alive mismatch.", outFail)) return false;
    if (!expect(col == 1 && row == 1, "world_list_units grid mismatch.", outFail)) return false;

    // Move accessors should return configured moves.
    sol::function fastFn = lua["unit_fast_move"];
    sol::function chargedFn = lua["unit_charged_move"];
    if (!expect(fastFn.valid(), "unit_fast_move binding missing.", outFail)) return false;
    if (!expect(chargedFn.valid(), "unit_charged_move binding missing.", outFail)) return false;
    const std::string fastMove = fastFn(idA);
    const std::string chargedMove = chargedFn(idA);
    if (!expect(fastMove == "tackle", "unit_fast_move returned unexpected value.", outFail)) return false;
    if (!expect(chargedMove == "seed_bomb", "unit_charged_move returned unexpected value.", outFail)) return false;

    // move_get should expose move metadata.
    sol::function moveGet = lua["move_get"];
    if (!expect(moveGet.valid(), "move_get binding missing.", outFail)) return false;
    sol::table moveT = moveGet("tackle");
    if (!expect(moveT.valid(), "move_get returned invalid table.", outFail)) return false;
    const std::string kind = moveT["kind"].get_or(std::string());
    if (!expect(kind == "fast", "move_get kind mismatch for tackle.", outFail)) return false;

    // Nearest enemy cell + adjacency.
    sol::function nearestFn = lua["world_nearest_enemy_cell"];
    if (!expect(nearestFn.valid(), "world_nearest_enemy_cell binding missing.", outFail)) return false;
    std::pair<int, int> nearest = nearestFn(idA);
    if (!expect(nearest.first == 2 && nearest.second == 1, "world_nearest_enemy_cell mismatch.", outFail)) return false;

    sol::function adjacentFn = lua["world_is_adjacent_to_enemy"];
    if (!expect(adjacentFn.valid(), "world_is_adjacent_to_enemy binding missing.", outFail)) return false;
    const bool adjacent = adjacentFn(idA);
    if (!expect(adjacent == true, "world_is_adjacent_to_enemy expected true.", outFail)) return false;

    // Commit move queues and applies after flush.
    sol::function commitFn = lua["world_commit_move"];
    if (!expect(commitFn.valid(), "world_commit_move binding missing.", outFail)) return false;
    commitFn(idA, 1, 2);
    api.flush();
    if (!expect(units[0].isMoving, "commit_move did not mark unit as moving.", outFail)) return false;
    if (!expect(units[0].committedDest.x == 1 && units[0].committedDest.y == 2,
                "commit_move did not set committedDest.", outFail)) return false;

    // Apply move should snap to target and clear moving state.
    sol::function applyFn = lua["world_apply_move"];
    if (!expect(applyFn.valid(), "world_apply_move binding missing.", outFail)) return false;
    applyFn(idA, 1, 3);
    api.flush();
    const auto cell = worldToGrid(cfg, units[0].position);
    if (!expect(cell.x == 1 && cell.y == 3, "apply_move did not update position.", outFail)) return false;
    if (!expect(!units[0].isMoving, "apply_move did not clear moving state.", outFail)) return false;

    // Face enemy should change rotation.
    const float prevRot = units[0].rotation.y;
    sol::function faceFn = lua["world_face_enemy"];
    if (!expect(faceFn.valid(), "world_face_enemy binding missing.", outFail)) return false;
    faceFn(idA);
    api.flush();
    if (!expect(std::fabs(units[0].rotation.y - prevRot) > 0.01f, "world_face_enemy did not update rotation.", outFail)) return false;

    // Emit + drain events should return a payload.
    sol::function emitFn = lua["emit"];
    if (!expect(emitFn.valid(), "emit binding missing.", outFail)) return false;
    emitFn("test_event", "payload");
    api.flush();
    sol::function drainFn = lua["events_drain"];
    if (!expect(drainFn.valid(), "events_drain binding missing.", outFail)) return false;
    sol::table eventsTbl = drainFn();
    if (!expect(eventsTbl.size() == 1, "events_drain expected one event.", outFail)) return false;
    sol::table e = eventsTbl[1];
    const std::string evtType = e["type"].get_or(std::string());
    const std::string evtPayload = e["payload"].get_or(std::string());
    if (!expect(evtType == "test_event", "events_drain type mismatch.", outFail)) return false;
    if (!expect(evtPayload == "payload", "events_drain payload mismatch.", outFail)) return false;

    return true;
}
