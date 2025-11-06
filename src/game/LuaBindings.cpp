// LuaBindings.cpp

#include "LuaBindings.h"
#include "GameWorld.h"
#include "PokemonInstance.h"
#include "../engine/events/EventManager.h"
#include "../engine/events/RoundEvents.h"

void registerLuaBindings(sol::state& lua, GameWorld* world) {
    // ---- Enums / structs exposed to Lua ----
    lua.new_enum("PokemonSide",
        "Player", PokemonSide::Player,
        "Enemy",  PokemonSide::Enemy
    );

    lua.new_usertype<PokemonInstance>("PokemonInstance",
        "name",     &PokemonInstance::name,
        "position", &PokemonInstance::position,
        "rotation", &PokemonInstance::rotation,
        "side",     &PokemonInstance::side,
        "hp",       &PokemonInstance::hp,
        "attack",   &PokemonInstance::attack,
        "alive",    &PokemonInstance::alive
    );

    // ---- Minimal world API for scripts already used elsewhere ----
    lua["spawnPokemon"] = [world](std::string name, float x, float y, float z) {
        world->spawnPokemon(name, glm::vec3(x, y, z));
    };

    // Direct reference to pokemon vector if needed by scripts
    lua["pokemons"] = &world->getPokemons();

    // ---- Round events emission for scripted RoundSystem ----
    // Lua calls: emit_round_phase_changed("Planning", "Battle")
    lua.set_function("emit_round_phase_changed",
        [](const std::string& prev, const std::string& next) {
            RoundPhaseChangedEvent evt(prev, next);
            EventManager::getInstance().emit(evt);
        }
    );
}
