// MovementSystem.cpp
#include "MovementSystem.h"
#include "../LuaBindings.h"
#include <iostream>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

class GridOccupancy;

MovementSystem::MovementSystem(GameWorld* world)
    : gameWorld(world)
{
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
    registerLuaBindings(lua, gameWorld, /*GameStateManager*/ nullptr);
    exposeConstants();
    loadScript();
}

MovementSystem::MovementSystem(GameWorld* world, const GridOccupancy& /*unused*/)
    : MovementSystem(world) {}

void MovementSystem::exposeConstants() {
    // Make grid constants available to Lua scripts
    lua["GRID_COLS"]  = GRID_COLS;
    lua["GRID_ROWS"]  = GRID_ROWS;
    lua["CELL_SIZE"]  = CELL_SIZE;
}

void MovementSystem::loadScript() {
    // Load and run the Lua movement system
    sol::load_result chunk = lua.load_file("scripts/systems/movement.lua");
    if (!chunk.valid()) {
        sol::error e = chunk;
        std::cerr << "[MovementSystem] Failed to load movement.lua: " << e.what() << "\n";
        ok = false;
        return;
    }

    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error e = r;
        std::cerr << "[MovementSystem] Failed to execute movement.lua: " << e.what() << "\n";
        ok = false;
        return;
    }

    // Optional init function in Lua
    sol::function init = lua["movement_init"];
    if (init.valid()) {
        sol::protected_function_result ir = init();
        if (!ir.valid()) {
            sol::error e = ir;
            std::cerr << "[MovementSystem] movement_init() error: " << e.what() << "\n";
            ok = false;
            return;
        }
    }

    ok = true;
}

void MovementSystem::update(float deltaTime) {
    if (!ok) return;

    // 1) Let Lua compute winners and start committed one-cell moves.
    if (sol::function updateFn = lua["movement_update"]; updateFn.valid()) {
        sol::protected_function_result ur = updateFn(deltaTime);
        if (!ur.valid()) {
            sol::error e = ur;
            std::cerr << "[MovementSystem] movement_update(dt) error: " << e.what() << "\n";
            ok = false; // prevent spamming on repeated errors
            return;
        }
    }

    // 2) Advance interpolation for units that have an active commit
    //    Distance per second is movementSpeed * CELL_SIZE (cells/sec * worldUnitsPerCell).
    if (!gameWorld) return;
    auto& units = gameWorld->getPokemons();

    for (auto& u : units) {
        if (!u.alive) continue;
        if (!u.isMoving) continue;

        const glm::vec3 toVec = u.moveTo - u.position;
        const float dist = glm::length(toVec);
        if (dist <= 1e-4f) {
            // Arrived (or already there)
            u.position = u.moveTo;
            u.isMoving = false;
            u.moveT = 1.0f;
            u.committedDest = {-1,-1};
            continue;
        }

        const glm::vec3 dir = toVec / dist;
        const float step = u.movementSpeed * CELL_SIZE * deltaTime; // world units per frame
        if (step >= dist) {
            // Finish the move this frame
            u.position = u.moveTo;
            u.isMoving = false;
            u.moveT = 1.0f;
            u.committedDest = {-1,-1};
        } else {
            // Advance toward destination
            u.position += dir * step;
            // Rough progress: step / one-cell distance (CELL_SIZE)
            u.moveT = std::min(1.0f, u.moveT + (step / (CELL_SIZE + 1e-4f)));
        }
    }
}
