// MovementExecutor.cpp

#include "MovementExecutor.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#define LOG(x) std::cout << "[MovementExecutor] " << x << std::endl;

MovementExecutor::MovementExecutor(GameWorld* world, int gridCols, int gridRows, float cellSize)
    : gameWorld(world)
    , gridCols(gridCols)
    , gridRows(gridRows)
    , cellSize(cellSize)
{}

// Convert world coordinates to a grid cell.
glm::ivec2 MovementExecutor::worldToGrid(const glm::vec3& pos) const {
    float boardOriginX = -(gridCols * cellSize) / 2 + cellSize / 2;
    float boardOriginZ = -(gridRows * cellSize) / 2 + cellSize / 2;
    int col = static_cast<int>(std::round((pos.x - boardOriginX) / cellSize));
    int row = static_cast<int>(std::round((pos.z - boardOriginZ) / cellSize));
    return { col, row };
}

// Convert a grid cell to a world coordinate.
glm::vec3 MovementExecutor::gridToWorld(int col, int row) const {
    float boardOriginX = -(gridCols * cellSize) / 2 + cellSize / 2;
    float boardOriginZ = -(gridRows * cellSize) / 2 + cellSize / 2;
    return { boardOriginX + col * cellSize, 0.0f, boardOriginZ + row * cellSize };
}

uint32_t MovementExecutor::gridKey(int col, int row) const {
    return static_cast<uint32_t>(col) | (static_cast<uint32_t>(row) << 16);
}

bool MovementExecutor::isValidGridPosition(int col, int row) const {
    return col >= 0 && col < gridCols && row >= 0 && row < gridRows;
}

// Execute the planned moves for each unit.
// For every unit with a planned move, we smoothly move it toward the target cell
// and update occupancy. Units that have no move stay in place.
GridOccupancy MovementExecutor::executeMoves(
    const std::unordered_map<PokemonInstance*, glm::ivec2>& plannedMoves,
    float deltaTime)
{
    GridOccupancy tempGrid;
    auto& pokemons = gameWorld->getPokemons();

    for (auto& unit : pokemons) {
        if (!unit.alive)
            continue;

        if (plannedMoves.find(&unit) != plannedMoves.end()) {
            glm::ivec2 targetCell = plannedMoves.at(&unit);
            uint32_t targetKey = gridKey(targetCell.x, targetCell.y);
            glm::vec3 targetPos = gridToWorld(targetCell.x, targetCell.y);

            // Smoothly update the unit's position toward its target.
            glm::vec3 direction = targetPos - unit.position;
            float distanceToMove = unit.movementSpeed * cellSize * deltaTime;
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
            tempGrid.set(worldToGrid(targetPos).x,
                         worldToGrid(targetPos).y);
        } else {
            // If no move is planned, mark the unit's current grid cell as occupied.
            glm::ivec2 currentGrid = worldToGrid(unit.position);
            tempGrid.set(currentGrid.x, currentGrid.y);
        }
    }
    return tempGrid;
}

// Update each unit's rotation to face the nearest enemy.
void MovementExecutor::updateUnitRotations() {
    auto& pokemons = gameWorld->getPokemons();

    for (auto& unit : pokemons) {
        if (!unit.alive)
            continue;

        glm::vec3 enemyPos = gameWorld->getNearestEnemyPosition(unit);
        glm::vec3 lookDir  = glm::normalize(enemyPos - unit.position);
        unit.rotation.y   = glm::degrees(atan2f(lookDir.x, lookDir.z));
    }
}
