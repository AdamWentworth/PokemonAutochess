// src/game/scripting/LuaBindings.cpp
#include "LuaBindings.h"
#include "LuaBindings_Internal.h"

void registerLuaBindings(sol::state& lua, GameWorld* world, GameStateManager* manager, LogBus::Logger* logger) {
    registerLuaBindings_Core(lua, world, manager, logger);
    registerLuaBindings_World(lua, world, manager, logger);
    registerLuaBindings_UnitMove(lua, world, manager, logger);
}
