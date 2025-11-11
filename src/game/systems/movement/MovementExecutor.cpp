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

        // Ensure gridCell is up to date (in case someone set position externally)
        glm::ivec2 curCell = worldToGrid(unit.position);
        unit.gridCell = curCell;

        auto itPlan = plannedMoves.find(&unit);

        // If we are already moving, keep moving toward the committed target.
        if (unit.isMoving && itPlan != plannedMoves.end()) {
            const glm::ivec2 destCell = unit.committedDest; // what we previously committed
            glm::vec3 targetPos = gridToWorld(destCell.x, destCell.y);

            // Move by speed in world units
            glm::vec3 to = targetPos - unit.position;
            float d = glm::length(to);
            if (d > 1e-3f) {
                glm::vec3 dir = to / d;
                float step = unit.movementSpeed * cellSize * deltaTime;
                if (step >= d) {
                    unit.position = targetPos;
                    unit.isMoving = false;
                    unit.moveT = 1.0f;
                    unit.gridCell = destCell;
                    unit.committedDest = glm::ivec2(-1, -1);
                } else {
                    unit.position += dir * step;
                    // Progress is approximate: how much of one-cell distance we've traversed
                    unit.moveT = std::clamp(unit.moveT + step / (cellSize + 1e-4f), 0.0f, 1.0f);
                }
            } else {
                // Already at center, finalize
                unit.position   = targetPos;
                unit.isMoving   = false;
                unit.moveT      = 1.0f;
                unit.gridCell   = destCell;
                unit.committedDest = glm::ivec2(-1, -1);
            }

            // While moving, mark the destination cell as occupied to prevent swaps/overlaps.
            tempGrid.set(destCell.x, destCell.y);
            continue;
        }

        // Not currently moving:
        if (itPlan != plannedMoves.end()) {
            const glm::ivec2 destCell = itPlan->second;

            // If the plan says "stay" just occupy current cell.
            if (destCell == curCell) {
                tempGrid.set(curCell.x, curCell.y);
            } else {
                // Start a new committed one-cell move (DON'T snap).
                unit.isMoving      = true;
                unit.moveFrom      = unit.position;
                unit.moveTo        = gridToWorld(destCell.x, destCell.y);
                unit.moveT         = 0.0f;
                unit.committedDest = destCell;

                // Move a bit on this frame immediately (so it feels responsive)
                glm::vec3 to = unit.moveTo - unit.position;
                float d = glm::length(to);
                if (d > 1e-3f) {
                    glm::vec3 dir = to / d;
                    float step = unit.movementSpeed * cellSize * deltaTime;
                    if (step >= d) {
                        unit.position = unit.moveTo;
                        unit.isMoving = false;
                        unit.moveT = 1.0f;
                        unit.gridCell = destCell;
                        unit.committedDest = glm::ivec2(-1, -1);
                    } else {
                        unit.position += dir * step;
                        unit.moveT = std::clamp(step / (cellSize + 1e-4f), 0.0f, 1.0f);
                    }
                } else {
                    unit.position   = unit.moveTo;
                    unit.isMoving   = false;
                    unit.moveT      = 1.0f;
                    unit.gridCell   = destCell;
                    unit.committedDest = glm::ivec2(-1, -1);
                }

                // Occupancy: reserve the destination while in transit
                const glm::ivec2 occ = unit.isMoving ? destCell : unit.gridCell;
                tempGrid.set(occ.x, occ.y);
            }
        } else {
            // No plan: just occupy current grid.
            tempGrid.set(curCell.x, curCell.y);
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
