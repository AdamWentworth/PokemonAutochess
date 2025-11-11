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
#include <cmath> // for std::round

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

    // ---- Engine-safe spawners (already used by menu/flow) ----
    lua.set_function("spawnPokemon", [world](std::string name, float x, float y, float z) {
        if (world) world->spawnPokemon(name, {x, y, z});
    });
    lua.set_function("spawn_on_grid", [world](std::string name, int col, int row, std::string side) {
        if (world) world->spawnPokemonAtGrid(name, col, row, sideFromString(side));
    });

    // ---- Round events (already used) ----
    lua.set_function("emit_round_phase_changed",
        [](const std::string& prev, const std::string& next) {
            RoundPhaseChangedEvent evt(prev, next);
            EventManager::getInstance().emit(evt);
        }
    );

    // ---- State mgmt (already used by ScriptedState) ----
    lua.set_function("push_state", [manager, world](const std::string& scriptPath) {
        if (!manager) return;
        manager->pushState(std::make_unique<ScriptedState>(manager, world, scriptPath));
    });
    lua.set_function("pop_state", [manager]() { if (manager) manager->popState(); });

    // =================================================================
    // World/Unit inspection & mutation for Lua movement logic
    // =================================================================

    // List units on board (bench excluded) with immutable snapshot data Lua needs.
    lua.set_function("world_list_units", [world, &lua]() {
        sol::state_view L(lua);
        sol::table arr = L.create_table();

        if (!world) return arr;

        int i = 1;
        for (auto& u : world->getPokemons()) {
            sol::table t = L.create_table();
            t["id"]     = u.id;
            t["name"]   = u.name;
            t["side"]   = (u.side == PokemonSide::Player) ? "Player" : "Enemy";
            t["hp"]     = u.hp;
            t["attack"] = u.attack;
            t["speed"]  = u.movementSpeed;
            auto cell   = worldToGrid(u.position);
            t["col"]    = cell.x;
            t["row"]    = cell.y;
            t["alive"]  = u.alive;
            arr[i++]    = t;
        }
        return arr;
    });

    // Apply one grid move (snap).
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

    // NEW: Start a one-cell move with interpolation; movement progress is advanced in MovementSystem
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

    // Get nearest enemy grid cell for a unit id.
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

    // Set yaw to face nearest enemy (degrees). Optionally pass explicit target col,row.
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
        it->rotation.y = glm::degrees(atan2f(lookDir.x, lookDir.z));
    });

    // Grid helpers to/from world (Lua uses grid exclusively)
    lua.set_function("grid_to_world", [](int col, int row) {
        auto p = gridToWorld(col, row);
        return std::make_tuple(p.x, p.y, p.z);
    });
    lua.set_function("world_to_grid", [](float x, float y, float z) {
        auto c = worldToGrid(glm::vec3{x,y,z});
        return std::make_pair(c.x, c.y);
    });
}
