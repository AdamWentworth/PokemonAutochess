// GameWorld.h

#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>

class Model;
class Camera3D;

// Represents one Pokémon placed on the board
// with its own position/orientation (and a shared Model).
struct PokemonInstance {
    Model* model;          // from ResourceManager
    glm::vec3 position;    // where it is on the board
    // You could store rotation, scale, HP, etc. here, too.
};

// This class manages all "active" Pokémon on the board.
class GameWorld {
public:
    // Place a new Pokémon on the board
    void spawnPokemon(const std::string& pokemonName, const glm::vec3& startPos);

    // Draw all active Pokémon
    void drawAll(const Camera3D& camera);

    // Access to the container for any picking or iteration
    std::vector<PokemonInstance>& getPokemons() { return pokemons; }

private:
    std::vector<PokemonInstance> pokemons;
};
