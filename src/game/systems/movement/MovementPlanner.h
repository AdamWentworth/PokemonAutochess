// MovementPlanner.h

#pragma once

#include "../../GameWorld.h"
#include "AStarPathfinder.h"
#include <unordered_map>
#include <unordered_set>
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

    // Utility functions for coordinate conversion.
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

    // Helper to convert reserved cells mapping into an obstacles map for the pathfinder.
    std::unordered_map<uint32_t, bool> reservedCellsAsObstacles(
        const std::unordered_map<uint32_t, PokemonInstance*>& reservedCells) const;

    // Helper: Check if two grid cells are adjacent (including diagonally).
    static bool isAdjacent(const glm::ivec2& cell, const glm::ivec2& enemyCell);

    // Helper: Revised alternate move search that avoids candidate moves that have already been tried.
    glm::ivec2 findAlternateMove(PokemonInstance& unit,
                                 const glm::ivec2& currentGrid,
                                 const glm::ivec2& enemyGrid,
                                 const std::unordered_map<uint32_t, PokemonInstance*>& reservedCells,
                                 const std::unordered_set<uint32_t>& triedMoves) const;
};

