// MovementSystem.cpp

#include "MovementSystem.h"
#include "movement/MovementPlanner.h"  // For using the MovementPlanner
#include <algorithm>
#include <limits>
#include <vector>
#include <queue>
#include <cmath>
#include <iostream>

#define LOG(x) std::cout << "[MovementSystem] " << x << std::endl;

MovementSystem::MovementSystem(GameWorld* world, std::unordered_map<uint32_t, bool>& gridOccupancy)
    : gameWorld(world)
    , gridOccupancy(gridOccupancy)
{
}

// Converts a world coordinate to a grid cell.
glm::ivec2 MovementSystem::worldToGrid(const glm::vec3& pos) const {
    float boardOriginX = -(GRID_COLS * CELL_SIZE) / 2 + CELL_SIZE / 2;
    float boardOriginZ = -(GRID_ROWS * CELL_SIZE) / 2 + CELL_SIZE / 2;
    int col = static_cast<int>(std::round((pos.x - boardOriginX) / CELL_SIZE));
    int row = static_cast<int>(std::round((pos.z - boardOriginZ) / CELL_SIZE));
    return { col, row };
}

// Converts a grid cell to a world coordinate.
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

// Finds the nearest enemy position for a given unit.
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

void MovementSystem::update(float deltaTime) {
    auto& pokemons = gameWorld->getPokemons();

    // Instantiate the MovementPlanner to compute desired moves.
    MovementPlanner planner(gameWorld, GRID_COLS, GRID_ROWS, CELL_SIZE);
    std::unordered_map<PokemonInstance*, glm::ivec2> plannedMoves = planner.planMoves();

    std::unordered_map<uint32_t, bool> tempGrid;

    // Update each unit based on its planned move.
    for (auto& unit : pokemons) {
        if (!unit.alive)
            continue;

        if (plannedMoves.find(&unit) != plannedMoves.end()) {
            // Retrieve the target cell and compute its world coordinate.
            glm::ivec2 targetCell = plannedMoves[&unit];
            uint32_t targetKey = gridKey(targetCell.x, targetCell.y);
            glm::vec3 targetPos = gridToWorld(targetCell.x, targetCell.y);
            unitTargetPositions[unit.id] = targetPos;

            // Move the unit smoothly toward its target cell.
            glm::vec3 direction = targetPos - unit.position;
            float distanceToMove = unit.movementSpeed * CELL_SIZE * deltaTime;
            if (glm::length(direction) > 0.01f) {
                if (glm::length(direction) > distanceToMove) {
                    glm::vec3 moveDir = glm::normalize(direction);
                    unit.position += moveDir * distanceToMove;
                } else {
                    unit.position = targetPos;
                    LOG("SnapMove: Unit " << unit.id << " snapped to center at " 
                                           << targetPos.x << "," << targetPos.z);
                }
            }
            tempGrid[targetKey] = true;
        } else {
            // If there's no planned move, keep the unit in its current cell.
            glm::ivec2 currentGrid = worldToGrid(unit.position);
            tempGrid[gridKey(currentGrid.x, currentGrid.y)] = true;
        }

        // Update the unit's rotation to face the nearest enemy.
        glm::vec3 enemyPos = findNearestEnemyPosition(unit);
        glm::vec3 lookDir = glm::normalize(enemyPos - unit.position);
        unit.rotation.y = glm::degrees(atan2f(lookDir.x, lookDir.z));
    }

    gridOccupancy = tempGrid;
}
