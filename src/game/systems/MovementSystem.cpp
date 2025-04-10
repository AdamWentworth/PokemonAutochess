// MovementSystem.cpp

#include "MovementSystem.h"
#include <algorithm>
#include <limits>
#include <vector>
#include <queue>
#include <cmath>

// Internal structure for A* nodes.
struct Node {
    glm::ivec2 cell;
    float cost;      // Cost so far (g value)
    float priority;  // f = g + h

    // For use in the priority queue: smallest priority has highest precedence.
    bool operator>(const Node& other) const {
        return priority > other.priority;
    }
};

MovementSystem::MovementSystem(GameWorld* world, std::unordered_map<uint32_t, bool>& gridOccupancy)
    : gameWorld(world), gridOccupancy(gridOccupancy)
{}

///
/// Helper: Determines if a given cell is adjacent to the enemy cell (Chebyshev distance == 1).
///
bool isAdjacent(const glm::ivec2& cell, const glm::ivec2& enemyCell) {
    return std::max(std::abs(cell.x - enemyCell.x), std::abs(cell.y - enemyCell.y)) == 1;
}

///
/// Helper: A* pathfinder that finds a path from start to any cell adjacent to enemyCell,
/// using the current reserved grid (passed via obstacles). Returns a vector of grid cells,
/// from start to goal. If no path is found, returns an empty vector.
///
std::vector<glm::ivec2> MovementSystem::findPathAStar(
    const glm::ivec2& start,
    const glm::ivec2& enemyCell,
    const std::unordered_map<uint32_t, bool>& obstacles) const
{
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openSet;
    std::unordered_map<uint32_t, glm::ivec2> cameFrom;
    std::unordered_map<uint32_t, float> costSoFar;

    auto cellKey = [this](const glm::ivec2& cell) -> uint32_t {
        return gridKey(cell.x, cell.y);
    };

    auto heuristic = [&enemyCell](const glm::ivec2& cell) -> float {
        float dx = static_cast<float>(cell.x - enemyCell.x);
        float dy = static_cast<float>(cell.y - enemyCell.y);
        return std::sqrt(dx * dx + dy * dy);
    };

    Node startNode { start, 0.0f, heuristic(start) };
    openSet.push(startNode);
    costSoFar[cellKey(start)] = 0.0f;

    // Allow movement in eight directions.
    std::vector<glm::ivec2> directions = {
        glm::ivec2(-1,  0), glm::ivec2(1,  0),
        glm::ivec2(0, -1), glm::ivec2(0,  1),
        glm::ivec2(-1, -1), glm::ivec2(1,  1),
        glm::ivec2(-1,  1), glm::ivec2(1, -1)
    };

    while (!openSet.empty()) {
        Node current = openSet.top();
        openSet.pop();

        if (isAdjacent(current.cell, enemyCell)) {
            // Reconstruct path.
            std::vector<glm::ivec2> path;
            glm::ivec2 cur = current.cell;
            path.push_back(cur);
            while (cellKey(cur) != cellKey(start)) {
                cur = cameFrom[cellKey(cur)];
                path.push_back(cur);
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        for (const auto& dir : directions) {
            glm::ivec2 next = current.cell + dir;
            if (!isValidGridPosition(next.x, next.y))
                continue;
            // Skip neighbor if it is already reserved (by any unit).
            if (obstacles.count(gridKey(next.x, next.y)) > 0)
                continue;
            float moveCost = (std::abs(dir.x) + std::abs(dir.y) == 2) ? 1.414f : 1.0f;
            float newCost = current.cost + moveCost;
            uint32_t nextKey = cellKey(next);
            if (costSoFar.find(nextKey) == costSoFar.end() || newCost < costSoFar[nextKey]) {
                costSoFar[nextKey] = newCost;
                float priority = newCost + heuristic(next);
                openSet.push(Node{ next, newCost, priority });
                cameFrom[nextKey] = current.cell;
            }
        }
    }
    return std::vector<glm::ivec2>(); // No path found.
}

void MovementSystem::update(float deltaTime) {
    auto& pokemons = gameWorld->getPokemons();

    // --- OVERLAP RESOLUTION PASS ---
    // Ensure each unit occupies a unique grid cell.
    {
        std::unordered_map<uint32_t, PokemonInstance*> gridOccupants;
        for (auto& unit : pokemons) {
            if (!unit.alive)
                continue;
            glm::ivec2 cell = worldToGrid(unit.position);
            if (!isValidGridPosition(cell.x, cell.y))
                continue;
            uint32_t key = gridKey(cell.x, cell.y);
            if (gridOccupants.find(key) == gridOccupants.end()) {
                gridOccupants[key] = &unit;
            } else {
                // A unit is already here. Search for a free adjacent cell.
                bool found = false;
                glm::ivec2 newCell = cell;
                for (int dx = -1; dx <= 1 && !found; ++dx) {
                    for (int dy = -1; dy <= 1 && !found; ++dy) {
                        if (dx == 0 && dy == 0)
                            continue;
                        glm::ivec2 candidate = cell + glm::ivec2(dx, dy);
                        if (!isValidGridPosition(candidate.x, candidate.y))
                            continue;
                        uint32_t candidateKey = gridKey(candidate.x, candidate.y);
                        if (gridOccupants.find(candidateKey) == gridOccupants.end()) {
                            newCell = candidate;
                            found = true;
                        }
                    }
                }
                if (found) {
                    unit.position = gridToWorld(newCell.x, newCell.y);
                    gridOccupants[gridKey(newCell.x, newCell.y)] = &unit;
                }
                // If no free neighbor is found, the unit remains in its cell.
            }
        }
    }
    // --- END OVERLAP RESOLUTION ---

    // First pass: Reserve current positions.
    std::unordered_map<uint32_t, bool> tempGrid;
    for (auto& unit : pokemons) {
        if (!unit.alive)
            continue;
        glm::ivec2 currentGrid = worldToGrid(unit.position);
        if (!isValidGridPosition(currentGrid.x, currentGrid.y))
            continue;
        tempGrid[gridKey(currentGrid.x, currentGrid.y)] = true;
    }

    // Second pass: Process movement for each unit.
    for (auto& unit : pokemons) {
        if (!unit.alive)
            continue;
        glm::ivec2 currentGrid = worldToGrid(unit.position);
        if (!isValidGridPosition(currentGrid.x, currentGrid.y))
            continue;

        // Determine target based on nearest enemy.
        glm::vec3 enemyPos = findNearestEnemyPosition(unit);
        glm::ivec2 enemyGrid = worldToGrid(enemyPos);

        glm::ivec2 desiredGrid = currentGrid;
        bool canMove = false;

        // If already adjacent, remain in place.
        if (isAdjacent(currentGrid, enemyGrid)) {
            desiredGrid = currentGrid;
        } else {
            // Otherwise, use A* to get a path toward a cell adjacent to the enemy.
            std::vector<glm::ivec2> path = findPathAStar(currentGrid, enemyGrid, tempGrid);
            if (path.size() > 1) {
                desiredGrid = path[1];
                canMove = true;
            } else {
                desiredGrid = currentGrid;
            }
        }
        
        // Update grid reservation if moving.
        if (canMove) {
            tempGrid.erase(gridKey(currentGrid.x, currentGrid.y));
            tempGrid[gridKey(desiredGrid.x, desiredGrid.y)] = true;
        }
        glm::vec3 targetPos = gridToWorld(desiredGrid.x, desiredGrid.y);

        // Smooth movement to the target grid cell.
        glm::vec3 direction = targetPos - unit.position;
        float distanceToMove = unit.movementSpeed * CELL_SIZE * deltaTime;
        
        if (glm::length(direction) > distanceToMove) {
            glm::vec3 moveDir = glm::normalize(direction);
            unit.position += moveDir * distanceToMove;
        } else {
            unit.position = targetPos;  // Snap to center.
        }

        // Update rotation so the unit faces its enemy target.
        glm::vec3 lookDir = glm::normalize(gridToWorld(enemyGrid.x, enemyGrid.y) - unit.position);
        unit.rotation.y = glm::degrees(atan2f(lookDir.x, lookDir.z));
    }

    // Commit new grid occupancy state.
    gridOccupancy = tempGrid;
}

glm::ivec2 MovementSystem::worldToGrid(const glm::vec3& pos) const {
    float boardOriginX = -(GRID_COLS * CELL_SIZE) / 2 + CELL_SIZE / 2;
    float boardOriginZ = -(GRID_ROWS * CELL_SIZE) / 2 + CELL_SIZE / 2;
    
    int col = static_cast<int>(std::round((pos.x - boardOriginX) / CELL_SIZE));
    int row = static_cast<int>(std::round((pos.z - boardOriginZ) / CELL_SIZE));
    return {col, row};
}

glm::vec3 MovementSystem::gridToWorld(int col, int row) const {
    float boardOriginX = -(GRID_COLS * CELL_SIZE) / 2 + CELL_SIZE / 2;
    float boardOriginZ = -(GRID_ROWS * CELL_SIZE) / 2 + CELL_SIZE / 2;
    
    return {
        boardOriginX + col * CELL_SIZE,
        0.0f,
        boardOriginZ + row * CELL_SIZE
    };
}

bool MovementSystem::isValidGridPosition(int col, int row) const {
    return col >= 0 && col < GRID_COLS && row >= 0 && row < GRID_ROWS;
}

uint32_t MovementSystem::gridKey(int col, int row) const {
    return static_cast<uint32_t>(col) | (static_cast<uint32_t>(row) << 16);
}

bool MovementSystem::isCellOccupied(int col, int row, const std::unordered_map<uint32_t, bool>& grid) const {
    return grid.count(gridKey(col, row)) > 0;
}

glm::vec3 MovementSystem::findNearestEnemyPosition(const PokemonInstance& unit) const {
    float closestDist = std::numeric_limits<float>::max();
    glm::vec3 closestPos = unit.position;

    for (const auto& other : gameWorld->getPokemons()) {
        if (!other.alive || other.side == unit.side)
            continue;
        
        float dist = glm::distance(unit.position, other.position);
        if (dist < closestDist) {
            closestDist = dist;
            closestPos = other.position;
        }
    }

    return closestPos;
}
