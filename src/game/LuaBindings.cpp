// LuaBindings.cpp

#include "LuaBindings.h"
#include "GameWorld.h"
#include "PokemonInstance.h"
#include "GameStateManager.h"
#include "ScriptedState.h"
#include "../engine/events/EventManager.h"
#include "../engine/events/RoundEvents.h"

static PokemonSide sideFromString(const std::string& s) {
    if (s == "Enemy" || s == "enemy")   return PokemonSide::Enemy;
    return PokemonSide::Player;
}

void registerLuaBindings(sol::state& lua, GameWorld* world, GameStateManager* manager) {
    // Enums / structs (minimal exposure)
    lua.new_enum("PokemonSide",
        "Player", PokemonSide::Player,
        "Enemy",  PokemonSide::Enemy
    );

    // World API
    lua.set_function("spawnPokemon", [world](std::string name, float x, float y, float z) {
        world->spawnPokemon(name, {x, y, z});
    });

    lua.set_function("spawn_on_grid", [world](std::string name, int col, int row, std::string side) {
        world->spawnPokemonAtGrid(name, col, row, sideFromString(side));
    });

    // Round events emission for scripted RoundSystem (already used)
    lua.set_function("emit_round_phase_changed",
        [](const std::string& prev, const std::string& next) {
            RoundPhaseChangedEvent evt(prev, next);
            EventManager::getInstance().emit(evt);
        }
    );

    // State management from Lua
    lua.set_function("push_state", [manager, world](const std::string& scriptPath) {
        manager->pushState(std::make_unique<ScriptedState>(manager, world, scriptPath));
    });
    lua.set_function("pop_state", [manager]() {
        manager->popState();
    });
}
