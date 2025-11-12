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

    std::shared_ptr<Model> model;

    // NEW: base (unscaled) stats from config
    int   baseHp = 100;
    int   baseAttack = 10;
    float baseMovementSpeed = 1.0f;

    // NEW: runtime level and scaled stats
    int level = 1;
    int maxHP = 100;          // scaled HP cap
    int hp = 100;             // current HP (<= maxHP)
    int attack = 10;          // scaled attack
    float movementSpeed = 1.0f; // scaled move speed

    bool alive = true;

    // --- Movement runtime state (one-cell commit) ---
    glm::ivec2 gridCell = glm::ivec2(-1, -1);
    bool isMoving = false;
    glm::vec3 moveFrom = {};
    glm::vec3 moveTo   = {};
    float moveT = 0.0f;
    glm::ivec2 committedDest = glm::ivec2(-1, -1);

    static int getNextUnitID() {
        static int nextID = 0;
        return nextID++;
    }
};
