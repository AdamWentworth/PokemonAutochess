// PokemonInstance.h

#pragma once
#include <glm/glm.hpp>
#include "../engine/render/Model.h"  // adjust the path if necessary

enum class PokemonSide {
    Player,
    Enemy
};

struct PokemonInstance {
    Model* model;          // shared model data from ResourceManager
    glm::vec3 position;    // board position
    glm::vec3 rotation;    // per-instance rotation (in degrees)
    PokemonSide side;      // distinguishes player from enemy Pokémon
};
