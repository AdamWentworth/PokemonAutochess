// GameWorld.h
#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "PokemonInstance.h"

class Camera3D;
class BoardRenderer; // NEW

class GameWorld {
public:
    void spawnPokemon(const std::string& pokemonName, const glm::vec3& startPos, PokemonSide side = PokemonSide::Player);
    void drawAll(const Camera3D& camera, BoardRenderer& boardRenderer); // UPDATED

    std::vector<PokemonInstance>& getPokemons();
    const PokemonInstance* getPokemonByName(const std::string& name) const;

    void addToBench(const std::string& pokemonName);
    std::vector<PokemonInstance>& getBenchPokemons();

private:
    std::vector<PokemonInstance> pokemons;
    std::vector<PokemonInstance> benchPokemons;
};
