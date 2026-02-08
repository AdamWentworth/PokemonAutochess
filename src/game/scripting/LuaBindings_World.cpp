// src/game/scripting/LuaBindings_World.cpp
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "LuaBindings.h"
#include "game/GameConfig.h"
#include "game/scripting/ScriptAPI.h"
#include "LuaBindings_Internal.h"

void registerLuaBindings_World(sol::state& lua, ScriptAPI& api) {
    const GameConfigData* cfg = &api.config();

    lua.set_function("world_list_units", [&api, &lua]() {
        sol::state_view L(lua);
        sol::table arr = L.create_table();
        int i = 1;
        for (const auto& u : api.listUnits()) {
            sol::table t = L.create_table();
            t["id"]        = u.id;
            t["name"]      = u.name;
            t["side"]      = (u.side == PokemonSide::Player) ? "Player" : "Enemy";
            t["hp"]        = u.hp;
            t["attack"]    = u.attack;
            t["speed"]     = u.speed;
            t["energy"]    = u.energy;
            t["maxEnergy"] = u.maxEnergy;
            t["col"]       = u.col;
            t["row"]       = u.row;
            t["alive"]     = u.alive;
            t["fastMove"]  = u.fastMove;
            t["chargedMove"] = u.chargedMove;
            arr[i++]       = t;
        }
        return arr;
    });

    lua.set_function("world_get_unit_snapshot", [&api, &lua](int unitId) {
        sol::state_view L(lua);
        sol::table t = L.create_table();
        auto snap = api.getUnitSnapshot(unitId);
        if (!snap.has_value()) return t;

        t["id"]        = snap->id;
        t["name"]      = snap->name;
        t["side"]      = (snap->side == PokemonSide::Player) ? "Player" : "Enemy";
        t["hp"]        = snap->hp;
        t["attack"]    = snap->attack;
        t["alive"]     = snap->alive;
        t["energy"]    = snap->energy;
        t["maxEnergy"] = snap->maxEnergy;
        t["fastMove"]  = snap->fastMove;
        t["chargedMove"] = snap->chargedMove;
        t["col"]       = snap->col;
        t["row"]       = snap->row;
        return t;
    });

    lua.set_function("world_apply_move", [&api](int unitId, int col, int row) {
        return api.applyMove(unitId, col, row);
    });

    lua.set_function("world_commit_move", [&api](int unitId, int col, int row) {
        return api.commitMove(unitId, col, row);
    });

    lua.set_function("world_nearest_enemy_cell", [&api](int unitId) {
        return api.nearestEnemyCell(unitId);
    });

    lua.set_function("world_is_adjacent_to_enemy", [&api](int unitId) {
        return api.isAdjacentToEnemy(unitId);
    });

    lua.set_function("world_enemies_adjacent", [&api, &lua](int unitId) {
        sol::state_view L(lua);
        sol::table arr = L.create_table();
        auto ids = api.enemiesAdjacent(unitId);
        int idx = 1;
        for (int id : ids) arr[idx++] = id;
        return arr;
    });

    lua.set_function("world_can_attack", [&api](int unitId) {
        return api.canAttack(unitId);
    });

    lua.set_function("world_attack_ready", [&api](int unitId) {
        return api.attackReady(unitId);
    });

    lua.set_function("world_apply_damage",
        [&api](int attackerId,
                int targetId,
                int amount,
                sol::optional<float> cadenceSec,
                sol::optional<std::string> moveName,
                sol::optional<std::string> kind) {
        return api.applyDamage(
            attackerId,
            targetId,
            amount,
            cadenceSec ? std::optional<float>(*cadenceSec) : std::nullopt,
            moveName ? std::optional<std::string>(*moveName) : std::nullopt,
            kind ? std::optional<std::string>(*kind) : std::nullopt
        );
    });

    lua.set_function("world_face_enemy", [&api](int unitId, sol::optional<int> tgtCol, sol::optional<int> tgtRow) {
        api.faceEnemy(unitId,
                      tgtCol ? std::optional<int>(*tgtCol) : std::nullopt,
                      tgtRow ? std::optional<int>(*tgtRow) : std::nullopt);
    });

    // Grid converters
    lua.set_function("grid_to_world", [cfg](int col, int row) {
        auto p = gridToWorld(*cfg, col, row);
        return std::make_tuple(p.x, p.y, p.z);
    });
    lua.set_function("world_to_grid", [cfg](float x, float y, float z) {
        auto c = worldToGrid(*cfg, glm::vec3{x,y,z});
        return std::make_pair(c.x, c.y);
    });

    // Optional: per-move tuning for minimum seconds between attack requests.
    // Returns 0 when no override exists (scripts should fall back to their defaults).
    lua.set_function("world_attack_min_request_sec",
        [&api](int attackerId, sol::optional<std::string> moveName, sol::optional<std::string> kind) -> float {
            return api.attackMinRequestSec(attackerId,
                                           moveName ? std::optional<std::string>(*moveName) : std::nullopt,
                                           kind ? std::optional<std::string>(*kind) : std::nullopt);
        });

    // ----- Energy helpers -----

    lua.set_function("world_get_energy", [&api](int unitId) {
        return api.getEnergy(unitId);
    });

    lua.set_function("world_get_max_energy", [&api](int unitId) {
        return api.getMaxEnergy(unitId);
    });

    lua.set_function("world_set_energy", [&api](int unitId, int value) {
        return api.setEnergy(unitId, value);
    });

    lua.set_function("world_add_energy", [&api](int unitId, int delta) {
        return api.addEnergy(unitId, delta);
    });

    lua.set_function("world_get_unit_speed", [&api](int unitId) {
        return api.getUnitSpeed(unitId);
    });

    lua.set_function("world_has_planned_move", [&api](int unitId) {
        return api.hasPlannedMove(unitId);
    });

    lua.set_function("world_is_moving", [&api](int unitId) {
        return api.isMoving(unitId);
    });
}
