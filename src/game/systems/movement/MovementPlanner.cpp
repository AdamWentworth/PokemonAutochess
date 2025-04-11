// MovementPlanner.cpp

#include "MovementPlanner.h"
#include <algorithm>
#include <limits>
#include <queue>
#include <cmath>
#include <iostream>

#define LOG(x) std::cout << "[MovementPlanner] " << x << std::endl;

MovementPlanner::MovementPlanner(GameWorld* world, int gridCols, int gridRows, float cellSize)
    : gameWorld(world)
    , gridCols(gridCols)
    , gridRows(gridRows)
    , cellSize(cellSize)
    , pathfinder(gridCols, gridRows) // Initialize A* pathfinder with grid dimensions.
{}

// Convert a world coordinate to a grid cell.
glm::ivec2 MovementPlanner::worldToGrid(const glm::vec3& pos) const {
    float boardOriginX = -(gridCols * cellSize) / 2 + cellSize / 2;
    float boardOriginZ = -(gridRows * cellSize) / 2 + cellSize / 2;
    int col = static_cast<int>(std::round((pos.x - boardOriginX) / cellSize));
    int row = static_cast<int>(std::round((pos.z - boardOriginZ) / cellSize));
    return {col, row};
}

// Convert a grid cell to a world coordinate.
glm::vec3 MovementPlanner::gridToWorld(int col, int row) const {
    float boardOriginX = -(gridCols * cellSize) / 2 + cellSize / 2;
    float boardOriginZ = -(gridRows * cellSize) / 2 + cellSize / 2;
    return { boardOriginX + col * cellSize, 0.0f, boardOriginZ + row * cellSize };
}

bool MovementPlanner::isValidGridPosition(int col, int row) const {
    return col >= 0 && col < gridCols && row >= 0 && row < gridRows;
}

uint32_t MovementPlanner::gridKey(int col, int row) const {
    return static_cast<uint32_t>(col) | (static_cast<uint32_t>(row) << 16);
}

// Returns the position of the nearest enemy of a given unit.
glm::vec3 MovementPlanner::findNearestEnemyPosition(const PokemonInstance& unit) const {
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

// Local helper to check if two grid cells are adjacent (non-diagonally or diagonally).
static bool isAdjacent(const glm::ivec2& cell, const glm::ivec2& enemyCell) {
    return std::max(std::abs(cell.x - enemyCell.x), std::abs(cell.y - enemyCell.y)) == 1;
}

// planMoves encapsulates all planning logic: resolve overlaps, plan path, and resolve move conflicts.
std::unordered_map<PokemonInstance*, glm::ivec2> MovementPlanner::planMoves() {
    auto& pokemons = gameWorld->getPokemons();
    
    // --- PHASE 0: Resolve Overlapping Units ---
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
            bool found = false;
            glm::ivec2 newCell = cell;
            for (int dx = -1; dx <= 1 && !found; ++dx) {
                for (int dy = -1; dy <= 1 && !found; ++dy) {
                    if (dx == 0 && dy == 0) continue;
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
                LOG("Overlap: Unit " << unit.id << " moved from " << cell.x << "," << cell.y
                    << " to " << newCell.x << "," << newCell.y);
                unit.position = gridToWorld(newCell.x, newCell.y);
                gridOccupants[gridKey(newCell.x, newCell.y)] = &unit;
            }
        }
    }
    
    // --- PHASE 1: Plan Desired Moves ---
    std::unordered_map<uint32_t, bool> tempGrid; // temporary occupancy map for A* (can be refined)
    std::unordered_map<uint32_t, std::vector<PokemonInstance*>> cellContenders;
    std::unordered_map<PokemonInstance*, glm::ivec2> desiredMoves;

    for (auto& unit : pokemons) {
        if (!unit.alive)
            continue;
        glm::ivec2 currentGrid = worldToGrid(unit.position);
        if (!isValidGridPosition(currentGrid.x, currentGrid.y))
            continue;

        glm::vec3 enemyPos = findNearestEnemyPosition(unit);
        glm::ivec2 enemyGrid = worldToGrid(enemyPos);

        // If already adjacent to enemy, the unit will hold its position.
        if (isAdjacent(currentGrid, enemyGrid)) {
            desiredMoves[&unit] = currentGrid;
            continue;
        }

        // Compute a path from the current cell toward the enemy position.
        std::vector<glm::ivec2> path = pathfinder.findPath(currentGrid, enemyGrid, tempGrid);
        desiredMoves[&unit] = (path.size() > 1 ? path[1] : currentGrid);
        
        // Record contenders for the targeted cell (for conflict resolution).
        if (desiredMoves[&unit] != currentGrid) {
            uint32_t nextCellKey = gridKey(desiredMoves[&unit].x, desiredMoves[&unit].y);
            cellContenders[nextCellKey].push_back(&unit);
        }
    }

    // --- PHASE 2: Resolve Conflicts for the Same Target Cell ---
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
                // Other contenders stay in place.
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

    return desiredMoves;
}
