// GameWorld.h
#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "PokemonInstance.h"
#include "ui/HealthBarData.h" 

class Camera3D;
class BoardRenderer;

class GameWorld {
public:
    void spawnPokemon(const std::string& pokemonName, const glm::vec3& startPos, PokemonSide side = PokemonSide::Player);
    void drawAll(const Camera3D& camera, BoardRenderer& boardRenderer); // UPDATED

    std::vector<PokemonInstance>& getPokemons();
    const PokemonInstance* getPokemonByName(const std::string& name) const;

    void addToBench(const std::string& pokemonName);
    std::vector<PokemonInstance>& getBenchPokemons();

    std::vector<HealthBarData> getHealthBarData(const Camera3D& camera, int screenWidth, int screenHeight) const;

private:
    std::vector<PokemonInstance> pokemons;
    std::vector<PokemonInstance> benchPokemons;
};
