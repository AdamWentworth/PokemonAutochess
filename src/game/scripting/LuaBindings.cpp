// src/game/scripting/LuaBindings.cpp
#include "LuaBindings.h"
#include "LuaBindings_Internal.h"

void registerLuaBindings(sol::state& lua, GameWorld* world, GameStateManager* manager) {
    registerLuaBindings_Core(lua, world, manager);
    registerLuaBindings_World(lua, world, manager);
    registerLuaBindings_UnitMove(lua, world, manager);
}
