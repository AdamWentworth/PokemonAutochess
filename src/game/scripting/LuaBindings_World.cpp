// src/game/scripting/LuaBindings.cpp
#include <glm/glm.hpp>
#include "engine/render/Model.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

#include "LuaBindings.h"

#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/GameStateManager.h"
#include "game/GameConfig.h"
#include "game/scripting/ScriptAPI.h"

#include "game/animation/FlightLocomotion.h"
#include "game/animation/AttackAnimDebug.h"

#include "game/config/GameDataDb.h"
#include "game/config/MovesConfigLoader.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/AnimSetLoader.h"

#include "game/state/ScriptedState.h"

#include "game/logging/LoggerUtil.h"
#include "game/logging/DebugTrace.h"

#include "LuaBindings_Internal.h"

void registerLuaBindings_World(sol::state& lua, ScriptAPI& api) {
    GameWorld* world = api.world();
    GameStateManager* manager = api.manager();
    LogBus::Logger* logger = &api.logger();
    const GameConfigData* cfg = &api.config();
    lua.set_function("world_list_units", [world, &lua, cfg]() {
        sol::state_view L(lua);
        sol::table arr = L.create_table();
        if (!world) return arr;
        int i = 1;
        for (auto& u : world->getPokemons()) {
            sol::table t = L.create_table();
            t["id"]        = u.id;
            t["name"]      = u.name;
            t["side"]      = (u.side == PokemonSide::Player) ? "Player" : "Enemy";
            t["hp"]        = u.hp;
            t["attack"]    = u.attack;
            t["speed"]     = u.movementSpeed;
            t["energy"]    = u.energy;
            t["maxEnergy"] = u.maxEnergy;
            auto cell      = worldToGrid(*cfg, u.position);
            t["col"]       = cell.x;
            t["row"]       = cell.y;
            t["alive"]     = u.alive;
            t["fastMove"]  = u.fastMove;
            t["chargedMove"] = u.chargedMove;
            arr[i++]       = t;
        }
        return arr;
    });

    lua.set_function("world_get_unit_snapshot", [world, &lua, cfg](int unitId) {
        sol::state_view L(lua);
        sol::table t = L.create_table();
        if (!world) return t;

        auto* u = world->findUnitById(unitId);
        if (!u) return t;

        t["id"]        = u->id;
        t["name"]      = u->name;
        t["side"]      = (u->side == PokemonSide::Player) ? "Player" : "Enemy";
        t["hp"]        = u->hp;
        t["attack"]    = u->attack;
        t["alive"]     = u->alive;
        t["energy"]    = u->energy;
        t["maxEnergy"] = u->maxEnergy;
        t["fastMove"]  = u->fastMove;
        t["chargedMove"] = u->chargedMove;
        auto cell      = worldToGrid(*cfg, u->position);
        t["col"]       = cell.x;
        t["row"]       = cell.y;
        return t;
    });
    // Movement & adjacency helpers (unchanged)

    lua.set_function("world_apply_move", [&api](int unitId, int col, int row) {
        return api.applyMove(unitId, col, row);
    });

    lua.set_function("world_commit_move", [&api](int unitId, int col, int row) {
        return api.commitMove(unitId, col, row);
    });
    lua.set_function("world_nearest_enemy_cell", [world, cfg](int unitId) {
        if (!world) return std::make_pair(-1, -1);

        auto& list = world->getPokemons();

        // Find the querying unit
        const auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == unitId; });
        if (it == list.end()) return std::make_pair(-1, -1);

        const auto myCell = worldToGrid(*cfg, it->position);

        int best = std::numeric_limits<int>::max();
        glm::ivec2 bestCell(-1, -1);

        for (const auto& u : list) {
            if (!u.alive || u.side == it->side) continue;
            const auto ec = worldToGrid(*cfg, u.position);

            // Chebyshev distance matches your 8-connected neighborhood
            const int d = std::max(std::abs(myCell.x - ec.x), std::abs(myCell.y - ec.y));
            if (d < best) {
                best = d;
                bestCell = ec;
            }
        }

        return std::make_pair(bestCell.x, bestCell.y);
    });

    // FIX: use integer distances to avoid int->float C4244 warnings
    lua.set_function("world_is_adjacent_to_enemy", [world, cfg](int unitId) {
        if (!world) return false;
        auto& list = world->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == unitId; });
        if (it == list.end()) return false;
        auto myCell = worldToGrid(*cfg, it->position);

        int best = std::numeric_limits<int>::max();
        glm::ivec2 bestCell(-999,-999);
        for (auto& u : list) {
            if (!u.alive || u.side == it->side) continue;
            auto ec = worldToGrid(*cfg, u.position);
            const int d = std::max(std::abs(myCell.x - ec.x), std::abs(myCell.y - ec.y));
            if (d < best) { best = d; bestCell = ec; }
        }
        const int dx = std::abs(myCell.x - bestCell.x);
        const int dy = std::abs(myCell.y - bestCell.y);
        return std::max(dx, dy) == 1;
    });

    lua.set_function("world_enemies_adjacent", [world, &lua, cfg](int unitId) {
        sol::state_view L(lua);
        sol::table arr = L.create_table();
        if (!world) return arr;

        PokemonInstance* attacker = nullptr;
        for (auto& u : world->getPokemons()) if (u.id == unitId) { attacker = &u; break; }
        if (!attacker || !attacker->alive) return arr;

        auto ac = worldToGrid(*cfg, attacker->position);
        int idx = 1;
        for (auto& u : world->getPokemons()) {
            if (!u.alive || u.side == attacker->side) continue;
            auto ec = worldToGrid(*cfg, u.position);
            const int dx = std::abs(ac.x - ec.x);
            const int dy = std::abs(ac.y - ec.y);
            if (std::max(dx, dy) == 1) {
                arr[idx++] = u.id;
            }
        }
        return arr;
    });


    // Can this unit currently *initiate* an attack?
    // - non-fliers: true (if alive and not moving)
    // - fliers using visual-only flight: only true when grounded (prevents "ghost" hits mid takeoff/landing)
    lua.set_function("world_can_attack", [world](int unitId) {
        if (!world) return false;
        for (auto& u : world->getPokemons()) {
            if (u.id != unitId) continue;
            if (!u.alive) return false;
            // IMPORTANT: do NOT gate attacks on isMoving.
            // Some movement systems keep isMoving=true while "holding" in melee,
            // which would suppress all attacks.
            // We only block initiating attacks for visual-only airborne locomotion.
            if (u.usesAirLocomotion && FlightLocomotion::isAirborne(u)) return false;
            return true;
        }
        return false;
    });

    // Can this unit *start a new attack animation cycle right now*?
    // - blocks when an attack is already playing (attackTimerSec > 0)
    // - blocks when visual-only flight locomotion is airborne
    lua.set_function("world_attack_ready", [world](int unitId) {
        if (!world) return false;
        for (auto& u : world->getPokemons()) {
            if (u.id != unitId) continue;
            if (!u.alive) return false;
            if (u.usesAirLocomotion && FlightLocomotion::isAirborne(u)) return false;
            if (u.attackTimerSec > 0.0001f) return false;
            return true;
        }
        return false;
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
        [world, logger](int attackerId, sol::optional<std::string> moveName, sol::optional<std::string> kind) -> float {
            if (!world) return 0.0f;
            auto& list = world->getPokemons();
            auto A = std::find_if(list.begin(), list.end(),
                [&](const PokemonInstance& p){ return p.id == attackerId; });
            if (A == list.end()) return 0.0f;

            const std::string speciesLower = toLowerCopy(A->name);
            const std::string moveLower    = moveName ? toLowerCopy(*moveName) : "";
            std::string kindLower          = kind ? toLowerCopy(*kind) : "";

            const auto* data = world->getData();
            if (kindLower.empty() && !moveLower.empty()) {
                if (data) {
                    if (const MoveData* md = data->moves.getMove(moveLower)) {
                        kindLower = toLowerCopy(md->kind);
                    }
                }
            }
            if (kindLower.empty()) kindLower = "fast";

            return data
                ? data->attackAnims.getMinRequestSec(speciesLower, kindLower, moveLower, logger)
                : 0.0f;
        });

    // ----- Energy helpers -----

    lua.set_function("world_get_energy", [world](int unitId) {
        if (!world) return 0;
        if (auto* u = world->findUnitById(unitId)) return u->energy;
        return 0;
    });

    lua.set_function("world_get_max_energy", [world](int unitId) {
        if (!world) return 100;
        if (auto* u = world->findUnitById(unitId)) return u->maxEnergy;
        return 100;
    });

    lua.set_function("world_set_energy", [&api](int unitId, int value) {
        return api.setEnergy(unitId, value);
    });

    lua.set_function("world_add_energy", [&api](int unitId, int delta) {
        return api.addEnergy(unitId, delta);
    });
// ====== move accessors for Lua combat ======

    
}
