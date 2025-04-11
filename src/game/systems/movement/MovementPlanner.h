// MovementPlanner.h

#pragma once

#include "../../GameWorld.h"
#include "AStarPathfinder.h"
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "../../PokemonInstance.h"

// MovementPlanner encapsulates all planning logic for unit moves.
class MovementPlanner {
public:
    // Constructor takes the game world, grid dimensions and cell size.
    MovementPlanner(GameWorld* world, int gridCols, int gridRows, float cellSize);

    // planMoves computes the desired grid moves for all alive units.
    // Returns a mapping from a unit pointer to its target grid cell.
    std::unordered_map<PokemonInstance*, glm::ivec2> planMoves();

    // Utility functions for coordinate conversion
    glm::ivec2 worldToGrid(const glm::vec3& pos) const;
    glm::vec3 gridToWorld(int col, int row) const;
    bool isValidGridPosition(int col, int row) const;
    uint32_t gridKey(int col, int row) const;

private:
    GameWorld* gameWorld;
    int gridCols;
    int gridRows;
    float cellSize;
    AStarPathfinder pathfinder;

    // Finds the position of the nearest enemy for a given unit.
    glm::vec3 findNearestEnemyPosition(const PokemonInstance& unit) const;
};
