// AStarPathfinder.h

#pragma once

#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

// The AStarPathfinder class encapsulates the A* search algorithm.
// It assumes that the world grid is defined by a fixed number of columns and rows.
class AStarPathfinder {
public:
    // Constructs a pathfinder for a grid of the specified dimensions.
    AStarPathfinder(int gridCols, int gridRows);

    // Performs an A* search starting from 'start' toward a target.
    // The search stops once a node adjacent to 'target' is reached.
    // 'obstacles' is an occupancy map keyed by cell indices.
    std::vector<glm::ivec2> findPath(
        const glm::ivec2& start,
        const glm::ivec2& target,
        const std::unordered_map<uint32_t, bool>& obstacles) const;

private:
    int gridCols;
    int gridRows;

    // Checks if a cell is within the grid bounds.
    bool isValidGridPosition(const glm::ivec2& cell) const;

    // Returns true if 'cell' is immediately adjacent to 'target' (in any of the 8 directions).
    static bool isAdjacent(const glm::ivec2& cell, const glm::ivec2& target);

    // Computes a unique key for a given grid cell.
    static uint32_t gridKey(int col, int row);
};
