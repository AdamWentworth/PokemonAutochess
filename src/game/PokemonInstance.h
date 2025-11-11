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

    // --- Movement runtime state (one-cell commit) ---
    // Current integer grid the unit is at (kept in sync by MovementExecutor).
    glm::ivec2 gridCell = glm::ivec2(-1, -1);

    // If true, the unit is currently interpolating toward moveTo / committedDest.
    bool isMoving = false;

    // World-space start/end for the current committed move and t accumulator.
    glm::vec3 moveFrom = {};
    glm::vec3 moveTo   = {};
    float moveT = 0.0f;  // 0..1 progress (optional; executor advances it)

    // Destination cell we have committed to this step; (-1,-1) if none.
    glm::ivec2 committedDest = glm::ivec2(-1, -1);

    static int getNextUnitID() {
        static int nextID = 0;
        return nextID++;
    }
};
