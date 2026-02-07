// src/game/scripting/LuaBindings_Internal.h
#pragma once

#include <string>
#include <glm/glm.hpp>

class GameWorld;
class GameStateManager;
namespace sol { class state; }
namespace LogBus { class Logger; }
struct GameConfigData;

// PokemonSide is declared in game/PokemonInstance.h as a scoped enum (enum class).
// Forward declare it with the same kind.
enum class PokemonSide;

// ---- shared helpers (defined in LuaBindings_Util.cpp) ----
std::string toLowerCopy(std::string s);
int animIndexCached(class PokemonInstance& p, const std::string& clipName);
PokemonSide sideFromString(const std::string& s);

glm::vec3 gridToWorld(const GameConfigData* cfg, int col, int row);
glm::ivec2 worldToGrid(const GameConfigData* cfg, const glm::vec3& pos);

bool attackerIsInAttackAnimation(const class PokemonInstance& A);

// ---- module registrars (defined in separate .cpp files) ----
void registerLuaBindings_Core(sol::state& lua, GameWorld* world, GameStateManager* manager, LogBus::Logger* logger);
void registerLuaBindings_World(sol::state& lua, GameWorld* world, GameStateManager* manager, LogBus::Logger* logger);
void registerLuaBindings_UnitMove(sol::state& lua, GameWorld* world, GameStateManager* manager, LogBus::Logger* logger);
