// GameWorld.h
#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "PokemonInstance.h"
#include "../engine/ui/HealthBarData.h"

class Camera3D;
class BoardRenderer;

class GameWorld {
public:
    // level: -1 means use GameConfig::get().baseLevel
    void spawnPokemon(const std::string& pokemonName,
                      const glm::vec3& startPos,
                      PokemonSide side = PokemonSide::Player,
                      int level = -1);

    void spawnPokemonAtGrid(const std::string& pokemonName,
                            int col, int row,
                            PokemonSide side = PokemonSide::Player,
                            int level = -1);

    void drawAll(const Camera3D& camera, BoardRenderer& boardRenderer);

    std::vector<PokemonInstance>& getPokemons();
    const PokemonInstance* getPokemonByName(const std::string& name) const;

    void addToBench(const std::string& pokemonName);
    std::vector<PokemonInstance>& getBenchPokemons();

    std::vector<HealthBarData> getHealthBarData(const Camera3D& camera, int screenWidth, int screenHeight) const;

    glm::vec3 getNearestEnemyPosition(const PokemonInstance& unit) const;

private:
    std::vector<PokemonInstance> pokemons;
    std::vector<PokemonInstance> benchPokemons;

    glm::vec3 gridToWorld(int col, int row) const;

    // helpers
    void applyLevelScaling(PokemonInstance& inst, int level) const;
    void applyLoadoutForLevel(PokemonInstance& inst) const;
};
