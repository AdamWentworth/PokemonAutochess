// src/game/scripting/LuaBindings.cpp
#include "LuaBindings.h"
#include "LuaBindings_Internal.h"

void registerLuaBindings(sol::state& lua, ScriptAPI& api) {
    registerLuaBindings_Core(lua, api);
    registerLuaBindings_World(lua, api);
    registerLuaBindings_UnitMove(lua, api);
}
