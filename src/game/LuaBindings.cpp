// LuaBindings.cpp

#include "LuaBindings.h"
#include "GameWorld.h"
#include "PokemonInstance.h"

void registerLuaBindings(sol::state& lua, GameWorld* world) {
    // Expose PokemonSide enum
    lua.new_enum("PokemonSide",
        "Player", PokemonSide::Player,
        "Enemy", PokemonSide::Enemy
    );

    // Expose PokemonInstance
    lua.new_usertype<PokemonInstance>("PokemonInstance",
        "name", &PokemonInstance::name,
        "position", &PokemonInstance::position,
        "rotation", &PokemonInstance::rotation,
        "side", &PokemonInstance::side,
        "hp", &PokemonInstance::hp,
        "attack", &PokemonInstance::attack,
        "alive", &PokemonInstance::alive
    );

    // Expose GameWorld interface
    lua["spawnPokemon"] = [world](std::string name, float x, float y, float z) {
        world->spawnPokemon(name, glm::vec3(x, y, z));
    };

    lua["pokemons"] = &world->getPokemons();  // direct reference to the vector
}
