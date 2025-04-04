// GameWorld.h
#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "PokemonInstance.h"  // new instance definition

class Camera3D;

class GameWorld {
public:
    // Spawn a new Pokémon with an optional side parameter (defaults to Player)
    void spawnPokemon(const std::string& pokemonName, const glm::vec3& startPos, PokemonSide side = PokemonSide::Player);

    // Draw all active Pokémon on the board using per-instance transformations.
    void drawAll(const Camera3D& camera);

    // Access for picking or iteration
    std::vector<PokemonInstance>& getPokemons() { return pokemons; }

private:
    std::vector<PokemonInstance> pokemons;
};
