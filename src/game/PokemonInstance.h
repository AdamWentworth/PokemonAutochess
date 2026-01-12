// src/game/PokemonInstance.h
#pragma once

#include <string>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class Model;

enum class PokemonSide {
    Player,
    Enemy
};

struct PokemonInstance {
    // identity
    int id = 0;
    std::string name;
    std::shared_ptr<Model> model;

    // transform (world)
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // degrees XYZ (you use rotation.y)

    PokemonSide side = PokemonSide::Player;
    bool alive = true;

    // leveling/stats
    int level = 1;

    int   baseHp = 100;
    int   baseAttack = 10;
    float baseMovementSpeed = 1.0f;

    int   hp = 100;
    int   maxHP = 100;
    int   attack = 10;
    float movementSpeed = 1.0f;

    // moves/energy
    std::string fastMove;
    std::string chargedMove;

    int energy = 0;
    int maxEnergy = 100;

    // movement interpolation (used by your Lua bindings)
    bool isMoving = false;
    glm::vec3 moveFrom{0.0f};
    glm::vec3 moveTo{0.0f};
    float moveT = 1.0f;
    glm::ivec2 committedDest{-1, -1};

    // NEW: per-instance animation time (seconds)
    float animTimeSec = 0.0f;

    static int getNextUnitID() {
        static int next = 1;
        return next++;
    }
};
