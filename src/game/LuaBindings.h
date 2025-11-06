// LuaBindings.h

#pragma once

#include <sol/sol.hpp>

class GameWorld;

// Registers all C++↔Lua bindings used by gameplay scripts.
void registerLuaBindings(sol::state& lua, GameWorld* world);
