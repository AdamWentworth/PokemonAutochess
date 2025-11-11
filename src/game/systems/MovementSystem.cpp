// MovementSystem.cpp
#include "MovementSystem.h"
#include "../LuaBindings.h"
#include <iostream>

// Add this tiny forward-declaration so the signature compiles
// without needing the full header here either.
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
    if (!ok) return; // only run if script loaded
    sol::function updateFn = lua["movement_update"];
    if (updateFn.valid()) {
        sol::protected_function_result ur = updateFn(deltaTime);
        if (!ur.valid()) {
            sol::error e = ur;
            std::cerr << "[MovementSystem] movement_update(dt) error: " << e.what() << "\n";
            ok = false; // prevent spamming on repeated errors
        }
    }
}