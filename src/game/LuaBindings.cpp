// LuaBindings.cpp
#include "LuaBindings.h"
#include "GameWorld.h"
#include "PokemonInstance.h"
#include "GameStateManager.h"
#include "ScriptedState.h"
#include "../engine/events/EventManager.h"
#include "../engine/events/RoundEvents.h"
#include "GameConfig.h"
#include <glm/glm.hpp>
#include <iostream>
#include <algorithm>
#include <cmath> // round, atan2f
#include "LogBus.h"

// Helper
static PokemonSide sideFromString(const std::string& s) {
    if (s == "Enemy" || s == "enemy") return PokemonSide::Enemy;
    return PokemonSide::Player;
}

// Grid helpers (mirror of GameWorld::gridToWorld)
static glm::vec3 gridToWorld(int col, int row) {
    const auto& cfg = GameConfig::get();
    float boardOriginX = -((cfg.cols * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    float boardOriginZ = -((cfg.rows * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    return { boardOriginX + col * cfg.cellSize, 0.0f, boardOriginZ + row * cfg.cellSize };
}
static glm::ivec2 worldToGrid(const glm::vec3& pos) {
    const auto& cfg = GameConfig::get();
    float boardOriginX = -((cfg.cols * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    float boardOriginZ = -((cfg.rows * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    int col = static_cast<int>(std::round((pos.x - boardOriginX) / cfg.cellSize));
    int row = static_cast<int>(std::round((pos.z - boardOriginZ) / cfg.cellSize));
    return { col, row };
}

void registerLuaBindings(sol::state& lua, GameWorld* world, GameStateManager* manager) {
    // Basic enums
    lua.new_enum("PokemonSide",
        "Player", PokemonSide::Player,
        "Enemy",  PokemonSide::Enemy
    );

    // ---- Engine-safe spawners ----
    lua.set_function("spawnPokemon", [world](std::string name, float x, float y, float z) {
        if (world) world->spawnPokemon(name, {x, y, z});
    });
    lua.set_function("spawn_on_grid",
    [world](std::string name, int col, int row, std::string side, sol::optional<int> level) {
        int lvl = level.value_or(-1);
        if (world) world->spawnPokemonAtGrid(name, col, row, sideFromString(side), lvl);
    });

    // ---- Round events ----
    lua.set_function("emit_round_phase_changed",
        [](const std::string& prev, const std::string& next) {
            RoundPhaseChangedEvent evt(prev, next);
            EventManager::getInstance().emit(evt);
        }
    );

    // ---- State mgmt ----
    lua.set_function("push_state", [manager, world](const std::string& scriptPath) {
        if (!manager) return;
        manager->pushState(std::make_unique<ScriptedState>(manager, world, scriptPath));
    });
    lua.set_function("pop_state", [manager]() { if (manager) manager->popState(); });

    // =================================================================
    // World/Unit inspection & mutation for Lua systems
    // =================================================================

    // List units on board (bench excluded)
    lua.set_function("world_list_units", [world, &lua]() {
        sol::state_view L(lua);
        sol::table arr = L.create_table();
        if (!world) return arr;
        int i = 1;
        for (auto& u : world->getPokemons()) {
            sol::table t = L.create_table();
            t["id"]       = u.id;
            t["name"]     = u.name;
            t["side"]     = (u.side == PokemonSide::Player) ? "Player" : "Enemy";
            t["hp"]       = u.hp;
            t["attack"]   = u.attack;
            t["speed"]    = u.movementSpeed;
            t["energy"]   = u.energy;
            t["maxEnergy"]= u.maxEnergy;
            auto cell     = worldToGrid(u.position);
            t["col"]      = cell.x;
            t["row"]      = cell.y;
            t["alive"]    = u.alive;
            arr[i++]      = t;
        }
        return arr;
    });

    // Snapshot for a single unit id
    lua.set_function("world_get_unit_snapshot", [world, &lua](int unitId) {
        sol::state_view L(lua);
        sol::table t = L.create_table();
        if (!world) return t;
        for (auto& u : world->getPokemons()) {
            if (u.id == unitId) {
                t["id"]        = u.id;
                t["name"]      = u.name;
                t["side"]      = (u.side == PokemonSide::Player) ? "Player" : "Enemy";
                t["hp"]        = u.hp;
                t["attack"]    = u.attack;
                t["alive"]     = u.alive;
                t["energy"]    = u.energy;
                t["maxEnergy"] = u.maxEnergy;
                auto cell      = worldToGrid(u.position);
                t["col"]       = cell.x;
                t["row"]       = cell.y;
                return t;
            }
        }
        return t; // empty
    });

    // Apply one grid move (snap)
    lua.set_function("world_apply_move", [world](int unitId, int col, int row) {
        if (!world) return false;
        auto& list = world->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == unitId; });
        if (it == list.end() || !it->alive) return false;
        it->position = gridToWorld(col, row);
        it->isMoving = false;
        it->moveT = 1.0f;
        it->committedDest = {-1,-1};
        return true;
    });

    // Start a one-cell move with interpolation
    lua.set_function("world_commit_move", [world](int unitId, int col, int row) {
        if (!world) return false;
        auto& list = world->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == unitId; });
        if (it == list.end() || !it->alive) return false;

        const auto target = gridToWorld(col,row);
        it->committedDest = {col,row};
        it->moveFrom      = it->position;
        it->moveTo        = target;
        it->moveT         = 0.0f;
        it->isMoving      = true;
        return true;
    });

    // Nearest enemy grid cell for a unit id
    lua.set_function("world_nearest_enemy_cell", [world](int unitId) {
        if (!world) return std::make_pair(-1, -1);
        auto& list = world->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == unitId; });
        if (it == list.end()) return std::make_pair(-1, -1);
        glm::vec3 pos = world->getNearestEnemyPosition(*it);
        auto cell = worldToGrid(pos);
        return std::make_pair(cell.x, cell.y);
    });

    // Is unit adjacent (8-neigh) to any enemy?
    lua.set_function("world_is_adjacent_to_enemy", [world](int unitId) {
        if (!world) return false;
        auto& list = world->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == unitId; });
        if (it == list.end()) return false;
        auto myCell = worldToGrid(it->position);
        glm::vec3 enemyPos = world->getNearestEnemyPosition(*it);
        auto e = worldToGrid(enemyPos);
        int dx = std::abs(myCell.x - e.x);
        int dy = std::abs(myCell.y - e.y);
        return std::max(dx, dy) == 1; // 8-neighborhood engagement
    });

    // Enumerate enemy ids adjacent to 'unitId'
    lua.set_function("world_enemies_adjacent", [world, &lua](int unitId) {
        sol::state_view L(lua);
        sol::table arr = L.create_table();
        if (!world) return arr;

        PokemonInstance* attacker = nullptr;
        for (auto& u : world->getPokemons()) if (u.id == unitId) { attacker = &u; break; }
        if (!attacker || !attacker->alive) return arr;

        auto ac = worldToGrid(attacker->position);
        int idx = 1;
        for (auto& u : world->getPokemons()) {
            if (!u.alive || u.side == attacker->side) continue;
            auto ec = worldToGrid(u.position);
            int dx = std::abs(ac.x - ec.x);
            int dy = std::abs(ac.y - ec.y);
            if (std::max(dx, dy) == 1) {
                arr[idx++] = u.id;
            }
        }
        return arr;
    });

    // Apply damage; if target dies, mark not alive.
    lua.set_function("world_apply_damage", [world](int attackerId, int targetId, int amount) {
        if (!world) return -1;
        auto& list = world->getPokemons();
        auto A = std::find_if(list.begin(), list.end(), [&](const PokemonInstance& p){ return p.id == attackerId; });
        auto T = std::find_if(list.begin(), list.end(), [&](const PokemonInstance& p){ return p.id == targetId; });
        if (A == list.end() || T == list.end()) return -1;
        if (!A->alive || !T->alive) return T->hp;
        T->hp = std::max(0, T->hp - std::max(0, amount));
        if (T->hp == 0) {
            T->alive = false;
            T->isMoving = false;
            T->committedDest = {-1,-1};
        }
        return T->hp;
    });

    // Face target (or nearest if none provided)
    lua.set_function("world_face_enemy", [world](int unitId, sol::optional<int> tgtCol, sol::optional<int> tgtRow) {
        if (!world) return;
        auto& list = world->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == unitId; });
        if (it == list.end()) return;

        glm::vec3 target;
        if (tgtCol && tgtRow) {
            target = gridToWorld(*tgtCol, *tgtRow);
        } else {
            target = world->getNearestEnemyPosition(*it);
        }
        glm::vec3 lookDir = glm::normalize(target - it->position);
        // radians→degrees manually to avoid header dependence
        it->rotation.y = std::atan2(lookDir.x, lookDir.z) * 180.0f / 3.14159265358979323846f;
    });

    // Grid helpers to/from world
    lua.set_function("grid_to_world", [](int col, int row) {
        auto p = gridToWorld(col, row);
        return std::make_tuple(p.x, p.y, p.z);
    });
    lua.set_function("world_to_grid", [](float x, float y, float z) {
        auto c = worldToGrid(glm::vec3{x,y,z});
        return std::make_pair(c.x, c.y);
    });

    // ----- Energy helpers -----
    lua.set_function("world_get_energy", [world](int unitId) {
        if (!world) return 0;
        for (auto& u : world->getPokemons()) if (u.id == unitId) return u.energy;
        return 0;
    });

    lua.set_function("world_get_max_energy", [world](int unitId) {
        if (!world) return 100;
        for (auto& u : world->getPokemons()) if (u.id == unitId) return u.maxEnergy;
        return 100;
    });

    lua.set_function("world_set_energy", [world](int unitId, int value) {
        if (!world) return false;
        for (auto& u : world->getPokemons()) if (u.id == unitId) {
            u.energy = std::max(0, std::min(value, u.maxEnergy));
            return true;
        }
        return false;
    });

    lua.set_function("world_add_energy", [world](int unitId, int delta) {
        if (!world) return 0;
        for (auto& u : world->getPokemons()) if (u.id == unitId) {
            int m = u.maxEnergy;
            u.energy = std::max(0, std::min(u.energy + delta, m));
            return u.energy;
        }
        return 0;
    });
}
