// LuaBindings.h

#pragma once
#include <sol/sol.hpp>

class ScriptAPI;

// Registers all C++↔Lua bindings used by gameplay scripts.
void registerLuaBindings(sol::state& lua, ScriptAPI& api);
