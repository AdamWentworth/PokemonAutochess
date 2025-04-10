// MovementSystem.cpp

#include "MovementSystem.h"
#include <algorithm>
#include <limits>
#include <vector>
#include <queue>
#include <cmath>
#include <iostream>

#define LOG(x) std::cout << "[MovementSystem] " << x << std::endl;

// Internal structure for A* nodes.
struct Node {
    glm::ivec2 cell;
    float cost;      // Cost so far (g value)
    float priority;  // f = g + h

    bool operator>(const Node& other) const {
        return priority > other.priority;
    }
};

MovementSystem::MovementSystem(GameWorld* world, std::unordered_map<uint32_t, bool>& gridOccupancy)
    : gameWorld(world), gridOccupancy(gridOccupancy)
{}

bool isAdjacent(const glm::ivec2& cell, const glm::ivec2& enemyCell) {
    return std::max(std::abs(cell.x - enemyCell.x), std::abs(cell.y - enemyCell.y)) == 1;
}

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

    // LOG("A*: Start from " << start.x << "," << start.y << " to near " << enemyCell.x << "," << enemyCell.y);

    Node startNode { start, 0.0f, heuristic(start) };
    openSet.push(startNode);
    costSoFar[cellKey(start)] = 0.0f;

    std::vector<glm::ivec2> directions = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {1, 1}, {-1, 1}, {1, -1}
    };

    while (!openSet.empty()) {
        Node current = openSet.top();
        openSet.pop();

        if (isAdjacent(current.cell, enemyCell)) {
            std::vector<glm::ivec2> path;
            glm::ivec2 cur = current.cell;
            path.push_back(cur);
            while (cellKey(cur) != cellKey(start)) {
                cur = cameFrom[cellKey(cur)];
                path.push_back(cur);
            }
            std::reverse(path.begin(), path.end());

            // LOG("A*: Path found with " << path.size() << " steps.");
            return path;
        }

        for (const auto& dir : directions) {
            glm::ivec2 next = current.cell + dir;
            if (!isValidGridPosition(next.x, next.y)) continue;
            if (obstacles.count(gridKey(next.x, next.y)) > 0) continue;

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

    LOG("A*: No path found from " << start.x << "," << start.y << " to enemy at " << enemyCell.x << "," << enemyCell.y);
    return {};
}

void MovementSystem::update(float deltaTime) {
    auto& pokemons = gameWorld->getPokemons();

    {
        std::unordered_map<uint32_t, PokemonInstance*> gridOccupants;
        for (auto& unit : pokemons) {
            if (!unit.alive) continue;
            glm::ivec2 cell = worldToGrid(unit.position);
            if (!isValidGridPosition(cell.x, cell.y)) continue;
            uint32_t key = gridKey(cell.x, cell.y);

            if (gridOccupants.find(key) == gridOccupants.end()) {
                gridOccupants[key] = &unit;
            } else {
                bool found = false;
                glm::ivec2 newCell = cell;
                for (int dx = -1; dx <= 1 && !found; ++dx) {
                    for (int dy = -1; dy <= 1 && !found; ++dy) {
                        if (dx == 0 && dy == 0) continue;
                        glm::ivec2 candidate = cell + glm::ivec2(dx, dy);
                        if (!isValidGridPosition(candidate.x, candidate.y)) continue;
                        uint32_t candidateKey = gridKey(candidate.x, candidate.y);
                        if (gridOccupants.find(candidateKey) == gridOccupants.end()) {
                            newCell = candidate;
                            found = true;
                        }
                    }
                }
                if (found) {
                    LOG("Overlap: Unit " << unit.id << " moved from " << cell.x << "," << cell.y
                        << " to " << newCell.x << "," << newCell.y);
                    unit.position = gridToWorld(newCell.x, newCell.y);
                    gridOccupants[gridKey(newCell.x, newCell.y)] = &unit;
                }
            }
        }
    }

    std::unordered_map<uint32_t, bool> tempGrid;
    std::unordered_map<uint32_t, std::vector<PokemonInstance*>> cellContenders;
    std::unordered_map<PokemonInstance*, glm::ivec2> desiredMoves;

    // PHASE 1: Plan all desired moves first
    for (auto& unit : pokemons) {
        if (!unit.alive) continue;
        glm::ivec2 currentGrid = worldToGrid(unit.position);
        if (!isValidGridPosition(currentGrid.x, currentGrid.y)) continue;

        glm::vec3 enemyPos = findNearestEnemyPosition(unit);
        glm::ivec2 enemyGrid = worldToGrid(enemyPos);

        if (isAdjacent(currentGrid, enemyGrid)) {
            desiredMoves[&unit] = currentGrid;
            continue;
        }

        std::vector<glm::ivec2> path = findPathAStar(currentGrid, enemyGrid, tempGrid);
        desiredMoves[&unit] = path.size() > 1 ? path[1] : currentGrid;
        
        if (desiredMoves[&unit] != currentGrid) {
            uint32_t nextCellKey = gridKey(desiredMoves[&unit].x, desiredMoves[&unit].y);
            cellContenders[nextCellKey].push_back(&unit);
        }
    }

    std::unordered_map<uint32_t, PokemonInstance*> cellWinners;
    for (const auto& [cellKey, contenders] : cellContenders) {
        if (contenders.size() > 1) {
            LOG("Conflict: " << contenders.size() << " units want cell key " << cellKey);
            PokemonInstance* winner = nullptr;
            float bestScore = std::numeric_limits<float>::max();
            for (PokemonInstance* unit : contenders) {
                glm::vec3 enemyPos = findNearestEnemyPosition(*unit);
                float distToEnemy = glm::distance(unit->position, enemyPos);
                if (distToEnemy < bestScore) {
                    bestScore = distToEnemy;
                    winner = unit;
                }
            }
            if (winner) {
                LOG("ConflictWinner: Unit " << winner->id << " wins cell " << cellKey);
                cellWinners[cellKey] = winner;
                for (PokemonInstance* unit : contenders) {
                    if (unit != winner) {
                        LOG("ConflictLoser: Unit " << unit->id << " stays in place due to conflict at cell " << cellKey);
                        desiredMoves[unit] = worldToGrid(unit->position);
                    }
                }
            }
        } else if (contenders.size() == 1) {
            cellWinners[cellKey] = contenders[0];
        }
    }

    tempGrid.clear();
    for (auto& unit : pokemons) {
        if (!unit.alive) continue;
        if (desiredMoves.find(&unit) != desiredMoves.end()) {
            glm::ivec2 targetCell = desiredMoves[&unit];
            uint32_t targetKey = gridKey(targetCell.x, targetCell.y);
            bool canMove = (cellWinners.find(targetKey) == cellWinners.end() || cellWinners[targetKey] == &unit);

            if (canMove) {
                tempGrid[targetKey] = true;
                glm::vec3 targetPos = gridToWorld(targetCell.x, targetCell.y);
                unitTargetPositions[unit.id] = targetPos;

                glm::vec3 direction = targetPos - unit.position;
                float distanceToMove = unit.movementSpeed * CELL_SIZE * deltaTime;

                if (glm::length(direction) > 0.01f) {
                    if (glm::length(direction) > distanceToMove) {
                        glm::vec3 moveDir = glm::normalize(direction);
                        unit.position += moveDir * distanceToMove;
                        // LOG("Move: Unit " << unit.id << " moved to " << unit.position.x << "," << unit.position.z);
                    } else {
                        unit.position = targetPos;
                        LOG("SnapMove: Unit " << unit.id << " snapped to center at " << targetPos.x << "," << targetPos.z);
                    }
                }
            } else {
                glm::ivec2 currentGrid = worldToGrid(unit.position);
                tempGrid[gridKey(currentGrid.x, currentGrid.y)] = true;
            }
        }

        glm::vec3 enemyPos = findNearestEnemyPosition(unit);
        glm::vec3 lookDir = glm::normalize(enemyPos - unit.position);
        unit.rotation.y = glm::degrees(atan2f(lookDir.x, lookDir.z));
    }

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
    return { boardOriginX + col * CELL_SIZE, 0.0f, boardOriginZ + row * CELL_SIZE };
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
        if (!other.alive || other.side == unit.side) continue;
        float dist = glm::distance(unit.position, other.position);
        if (dist < closestDist) {
            closestDist = dist;
            closestPos = other.position;
        }
    }
    return closestPos;
}
