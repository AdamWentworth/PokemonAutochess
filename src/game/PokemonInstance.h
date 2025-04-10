// PokemonInstance.h

#pragma once
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include "../engine/render/Model.h"

enum class PokemonSide {
    Player,
    Enemy
};

struct PokemonInstance {
    int id;
    std::string name;
    glm::vec3 position = {};
    glm::vec3 rotation = {};
    PokemonSide side = PokemonSide::Player;
    
    // Changed from a raw pointer to a shared pointer.
    std::shared_ptr<Model> model;
    
    int hp = 100;
    int attack = 10;
    float movementSpeed = 1.0f;
    bool alive = true;

    static int getNextUnitID() {
        static int nextID = 0;
        return nextID++;
    }
};