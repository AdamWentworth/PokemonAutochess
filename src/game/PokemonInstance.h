// PokemonInstance.h

#pragma once
#include <glm/glm.hpp>
#include <string>
#include "../engine/render/Model.h"

enum class PokemonSide {
    Player,
    Enemy
};

struct PokemonInstance {
    std::string name;

    glm::vec3 position = {};
    glm::vec3 rotation = {};

    PokemonSide side = PokemonSide::Player;
    Model* model = nullptr;

    int hp = 100;
    int attack = 10;
    float movementSpeed = 1.0f;
    bool alive = true;
};
