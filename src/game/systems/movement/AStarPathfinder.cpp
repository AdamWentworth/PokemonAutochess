// AStarPathfinder.cpp

#include "AStarPathfinder.h"
#include <queue>
#include <cmath>
#include <limits>
#include <algorithm>

// Internal structure used by the A* search algorithm.
struct Node {
    glm::ivec2 cell;
    float cost;      // Cost so far (g value)
    float priority;  // f = g + h

    bool operator>(const Node& other) const {
        return priority > other.priority;
    }
};

AStarPathfinder::AStarPathfinder(int gridCols, int gridRows)
    : gridCols(gridCols), gridRows(gridRows)
{}

bool AStarPathfinder::isValidGridPosition(const glm::ivec2& cell) const {
    return cell.x >= 0 && cell.x < gridCols && cell.y >= 0 && cell.y < gridRows;
}

bool AStarPathfinder::isAdjacent(const glm::ivec2& cell, const glm::ivec2& target) {
    return std::max(std::abs(cell.x - target.x), std::abs(cell.y - target.y)) == 1;
}

uint32_t AStarPathfinder::gridKey(int col, int row) {
    return static_cast<uint32_t>(col) | (static_cast<uint32_t>(row) << 16);
}

std::vector<glm::ivec2> AStarPathfinder::findPath(
    const glm::ivec2& start,
    const glm::ivec2& target,
    const std::unordered_map<uint32_t, bool>& obstacles) const
{
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openSet;
    std::unordered_map<uint32_t, glm::ivec2> cameFrom;
    std::unordered_map<uint32_t, float> costSoFar;

    auto computeKey = [](const glm::ivec2& cell) -> uint32_t {
        return static_cast<uint32_t>(cell.x) | (static_cast<uint32_t>(cell.y) << 16);
    };

    auto heuristic = [&target](const glm::ivec2& cell) -> float {
        float dx = static_cast<float>(cell.x - target.x);
        float dy = static_cast<float>(cell.y - target.y);
        return std::sqrt(dx * dx + dy * dy);
    };

    // Initialize the search with the starting node.
    Node startNode{ start, 0.0f, heuristic(start) };
    openSet.push(startNode);
    costSoFar[computeKey(start)] = 0.0f;

    // Eight-connected grid: horizontal, vertical, and diagonal movements.
    std::vector<glm::ivec2> directions = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {1, 1}, {-1, 1}, {1, -1}
    };

    while (!openSet.empty()) {
        Node current = openSet.top();
        openSet.pop();

        // Stop when we are adjacent to the target.
        if (isAdjacent(current.cell, target)) {
            std::vector<glm::ivec2> path;
            glm::ivec2 cur = current.cell;
            path.push_back(cur);
            while (computeKey(cur) != computeKey(start)) {
                cur = cameFrom[computeKey(cur)];
                path.push_back(cur);
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        // Explore neighbors.
        for (const auto& dir : directions) {
            glm::ivec2 next = current.cell + dir;
            if (!isValidGridPosition(next))
                continue;
            if (obstacles.count(computeKey(next)) > 0)
                continue;

            float moveCost = (std::abs(dir.x) + std::abs(dir.y) == 2) ? 1.414f : 1.0f;
            float newCost = current.cost + moveCost;
            uint32_t nextKey = computeKey(next);

            if (costSoFar.find(nextKey) == costSoFar.end() || newCost < costSoFar[nextKey]) {
                costSoFar[nextKey] = newCost;
                float priority = newCost + heuristic(next);
                openSet.push(Node{ next, newCost, priority });
                cameFrom[nextKey] = current.cell;
            }
        }
    }
    // If no path is found, return an empty vector.
    return {};
}

