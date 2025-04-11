// MovementSystem.h

#pragma once

#include "../../engine/core/IUpdatable.h"
#include "../GameWorld.h"
#include <unordered_map>
#include <glm/glm.hpp>
#include <vector>
#include "movement/MovementPlanner.h"  // For planning moves

// The MovementSystem coordinates the planning and execution of unit movement.
class MovementSystem : public IUpdatable {
public:
    MovementSystem(GameWorld* world, std::unordered_map<uint32_t, bool>& gridOccupancy);
    void update(float deltaTime) override;

    // Public grid interface functions.
    glm::ivec2 worldToGrid(const glm::vec3& pos) const;
    uint32_t gridKey(int col, int row) const;
    bool isValidGridPosition(int col, int row) const;
    glm::vec3 gridToWorld(int col, int row) const;

private:
    GameWorld* gameWorld;
    std::unordered_map<uint32_t, bool>& gridOccupancy;
    std::unordered_map<int, glm::vec3> unitTargetPositions;

    // Grid configuration.
    const float CELL_SIZE = 1.2f;
    const int GRID_COLS = 8;
    const int GRID_ROWS = 8;

    // Utility function to find nearest enemy position.
    glm::vec3 findNearestEnemyPosition(const PokemonInstance& unit) const;
};