// LuaBindings.h

#pragma once
#include <sol/sol.hpp>

class GameWorld;
class GameStateManager;
namespace LogBus { class Logger; }

// Registers all C++↔Lua bindings used by gameplay scripts.
void registerLuaBindings(sol::state& lua, GameWorld* world, GameStateManager* manager, LogBus::Logger* logger);
