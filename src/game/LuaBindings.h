// LuaBindings.h

#pragma once

#include <sol/sol.hpp>

class GameWorld;

void registerLuaBindings(sol::state& lua, GameWorld* world);
