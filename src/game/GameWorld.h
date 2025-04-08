// GameWorld.h
#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "PokemonInstance.h"  // new instance definition

class Camera3D;

class GameWorld {
public:
    void spawnPokemon(const std::string& pokemonName, const glm::vec3& startPos, PokemonSide side = PokemonSide::Player);
    void drawAll(const Camera3D& camera);

    std::vector<PokemonInstance>& getPokemons();
    const PokemonInstance* getPokemonByName(const std::string& name) const;

private:
    std::vector<PokemonInstance> pokemons;
};
