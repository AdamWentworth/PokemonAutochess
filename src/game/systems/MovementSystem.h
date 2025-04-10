// MovementSystem.h

#pragma once

#include "../../engine/core/IUpdatable.h"
#include "../GameWorld.h"
#include <unordered_map>
#include <glm/glm.hpp>
#include <vector>  // for std::vector

class MovementSystem : public IUpdatable {
public:
    MovementSystem(GameWorld* world, std::unordered_map<uint32_t, bool>& gridOccupancy);
    void update(float deltaTime) override;

    // Public grid interface
    glm::ivec2 worldToGrid(const glm::vec3& pos) const;
    uint32_t gridKey(int col, int row) const;
    bool isValidGridPosition(int col, int row) const;
    glm::vec3 gridToWorld(int col, int row) const;

private:
    GameWorld* gameWorld;
    std::unordered_map<uint32_t, bool>& gridOccupancy;
    const float CELL_SIZE = 1.2f;
    const int GRID_COLS = 8;
    const int GRID_ROWS = 8;

    // Implementation details
    bool isCellOccupied(int col, int row, const std::unordered_map<uint32_t, bool>& grid) const;
    glm::vec3 findNearestEnemyPosition(const PokemonInstance& unit) const;

    // Added declaration for the A* pathfinding helper
    std::vector<glm::ivec2> findPathAStar(
        const glm::ivec2& start,
        const glm::ivec2& enemyCell,
        const std::unordered_map<uint32_t, bool>& obstacles) const;
};
