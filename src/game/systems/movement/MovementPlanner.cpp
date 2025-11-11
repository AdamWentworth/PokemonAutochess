// MovementPlanner.cpp

#include "MovementPlanner.h"
#include <algorithm>
#include <limits>
#include <queue>
#include <cmath>
#include <iostream>

#define LOG(x) std::cout << "[MovementPlanner] " << x << std::endl;

static inline bool validCell(const glm::ivec2& c) { return c.x >= 0 && c.y >= 0; }

MovementPlanner::MovementPlanner(GameWorld* world, int gridCols, int gridRows, float cellSize)
    : gameWorld(world)
    , gridCols(gridCols)
    , gridRows(gridRows)
    , cellSize(cellSize)
    , pathfinder(gridCols, gridRows) // Initialize A* pathfinder with grid dimensions.
{}

// Converts a world coordinate to a grid cell.
glm::ivec2 MovementPlanner::worldToGrid(const glm::vec3& pos) const {
    float boardOriginX = -(gridCols * cellSize) / 2 + cellSize / 2;
    float boardOriginZ = -(gridRows * cellSize) / 2 + cellSize / 2;
    int col = static_cast<int>(std::round((pos.x - boardOriginX) / cellSize));
    int row = static_cast<int>(std::round((pos.z - boardOriginZ) / cellSize));
    return { col, row };
}

// Converts a grid cell to a world coordinate.
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

// Helper: Convert reservedCells mapping into an obstacles map for the pathfinder.
GridOccupancy MovementPlanner::reservedCellsAsObstacles(
        const std::unordered_map<uint32_t, PokemonInstance*>& reserved) const
{
    GridOccupancy obst;
    for (auto& kv : reserved)
        obst.set(GridOccupancy::col(kv.first), GridOccupancy::row(kv.first));
    return obst;
}

// Helper: Check if two grid cells are adjacent (including diagonal neighbors).
bool MovementPlanner::isAdjacent(const glm::ivec2& cell, const glm::ivec2& enemyCell) {
    return std::max(std::abs(cell.x - enemyCell.x), std::abs(cell.y - enemyCell.y)) == 1;
}

// Helper: Revised alternate move search that uses a spiral search but skips cells in triedMoves.
glm::ivec2 MovementPlanner::findAlternateMove(PokemonInstance& unit,
    const glm::ivec2& currentGrid, const glm::ivec2& enemyGrid,
    const std::unordered_map<uint32_t, PokemonInstance*>& reservedCells,
    const std::unordered_set<uint32_t>& triedMoves) const 
{
    int maxRadius = 3; // Maximum search radius (tweak as needed).
    std::vector<glm::ivec2> candidates;

    // Iterate through increasing radii.
    for (int r = 1; r <= maxRadius; ++r) {
        // Loop through the perimeter of the square with radius r.
        for (int dx = -r; dx <= r; ++dx) {
            for (int dy = -r; dy <= r; ++dy) {
                // Only consider perimeter cells.
                if (std::abs(dx) != r && std::abs(dy) != r)
                    continue;
                glm::ivec2 candidate = currentGrid + glm::ivec2(dx, dy);
                if (!isValidGridPosition(candidate.x, candidate.y))
                    continue;
                uint32_t key = gridKey(candidate.x, candidate.y);
                // Only consider candidates that are not reserved and not already tried.
                if (reservedCells.find(key) == reservedCells.end() &&
                    triedMoves.find(key) == triedMoves.end()) {
                    candidates.push_back(candidate);
                }
            }
        }
        // If at least one candidate was found at this radius, return the first candidate.
        if (!candidates.empty())
            return candidates[0];
    }
    // Fallback to the current grid cell if no alternate found.
    return currentGrid;
}

// planMoves performs the following steps:
//  1. Phase 0 (Collision Handling): Resolve overlapping units by repositioning them.
//  2. Phase 1 (Predictive Move Planning): Use a sorted order and reservation system (with A* pathfinding)
//     to compute each unit's move. Units already moving are skipped and their destinations are pre-reserved.
std::unordered_map<PokemonInstance*, glm::ivec2> MovementPlanner::planMoves() {
    auto& pokemons = gameWorld->getPokemons();
    
    // --- PHASE 0: Resolve Overlapping Units ---
    std::unordered_map<uint32_t, PokemonInstance*> gridOccupants;
    for (auto& unit : pokemons) {
        if (!unit.alive)
            continue;
        glm::ivec2 cell = worldToGrid(unit.position);
        unit.gridCell = cell; // keep gridCell in sync always
        if (!isValidGridPosition(cell.x, cell.y))
            continue;
        uint32_t cellKey = gridKey(cell.x, cell.y);
        if (gridOccupants.find(cellKey) == gridOccupants.end()) {
            gridOccupants[cellKey] = &unit;
        } else {
            // Overlap detected: find a free adjacent cell.
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
                LOG("Overlap: Unit " << unit.id << " moved from [" << cell.x << "," << cell.y
                    << "] to [" << newCell.x << "," << newCell.y << "]");
                unit.position = gridToWorld(newCell.x, newCell.y);
                unit.gridCell = newCell;
                gridOccupants[gridKey(newCell.x, newCell.y)] = &unit;
            }
        }
    }
    
    // --- PHASE 1: Predictive Move Planning with Cycle Prevention ---
    std::unordered_map<PokemonInstance*, glm::ivec2> finalMoves;
    std::unordered_map<uint32_t, PokemonInstance*> reservedCells;

    // Pre-pass: seed reservations for units already moving, and ensure gridCell is set.
    for (PokemonInstance& u : pokemons) {
        if (!u.alive) continue;
        glm::ivec2 cg = worldToGrid(u.position);
        u.gridCell = cg;
        if (u.isMoving && validCell(u.committedDest)) {
            reservedCells[gridKey(u.committedDest.x, u.committedDest.y)] = &u;
        }
    }

    // Optional: sort units by priority (e.g., closeness to enemy).
    std::vector<PokemonInstance*> units;
    for (auto &unit : pokemons) {
        if (unit.alive)
            units.push_back(&unit);
    }
    std::sort(units.begin(), units.end(), [this](PokemonInstance* a, PokemonInstance* b) {
        glm::vec3 enemyA = gameWorld->getNearestEnemyPosition(*a);
        glm::vec3 enemyB = gameWorld->getNearestEnemyPosition(*b);
        float distA = glm::distance(a->position, enemyA);
        float distB = glm::distance(b->position, enemyB);
        return distA < distB;  // Prioritize unit closer to its enemy.
    });

    for (PokemonInstance* unit : units) {
        // Skip planning for units already in flight; mirror their committed destination.
        if (unit->isMoving) {
            if (validCell(unit->committedDest)) {
                finalMoves[unit] = unit->committedDest;
            } else {
                finalMoves[unit] = worldToGrid(unit->position);
            }
            continue;
        }

        glm::ivec2 currentGrid = worldToGrid(unit->position);
        glm::vec3 enemyPos = gameWorld->getNearestEnemyPosition(*unit);
        glm::ivec2 enemyGrid = worldToGrid(enemyPos);
        uint32_t currentKey = gridKey(currentGrid.x, currentGrid.y);

        // If adjacent to an enemy, hold position.
        if (isAdjacent(currentGrid, enemyGrid)) {
            if (reservedCells.find(currentKey) == reservedCells.end()) {
                finalMoves[unit] = currentGrid;
                reservedCells[currentKey] = unit;
            } else {
                // Use alternate move search if already reserved.
                std::unordered_set<uint32_t> triedMoves;
                const int MAX_ATTEMPTS = 3;
                int attempts = 0;
                glm::ivec2 candidate = currentGrid;
                while (attempts < MAX_ATTEMPTS) {
                    uint32_t candidateKey = gridKey(candidate.x, candidate.y);
                    triedMoves.insert(candidateKey);
                    if (reservedCells.find(candidateKey) == reservedCells.end())
                        break;
                    glm::ivec2 alt = findAlternateMove(*unit, currentGrid, enemyGrid, reservedCells, triedMoves);
                    // If no new candidate is found, fallback to holding position.
                    if (alt == candidate) {
                        candidate = currentGrid;
                        break;
                    }
                    candidate = alt;
                    attempts++;
                }
                finalMoves[unit] = candidate;
                reservedCells[gridKey(candidate.x, candidate.y)] = unit;
            }
            continue;
        }

        // Otherwise, compute primary move using A*.
        std::vector<glm::ivec2> path = pathfinder.findPath(
                        currentGrid, enemyGrid,
                        reservedCellsAsObstacles(reservedCells));
        glm::ivec2 primaryMove = (path.size() > 1 ? path[1] : currentGrid);
        uint32_t primaryKey = gridKey(primaryMove.x, primaryMove.y);

        if (reservedCells.find(primaryKey) == reservedCells.end()) {
            finalMoves[unit] = primaryMove;
            reservedCells[primaryKey] = unit;
        } else {
            // Try to find an alternate move while avoiding cycles.
            std::unordered_set<uint32_t> triedMoves;
            const int MAX_ATTEMPTS = 3;
            int attempts = 0;
            glm::ivec2 candidate = primaryMove;
            while (attempts < MAX_ATTEMPTS) {
                uint32_t candidateKey = gridKey(candidate.x, candidate.y);
                triedMoves.insert(candidateKey);
                if (reservedCells.find(candidateKey) == reservedCells.end())
                    break;
                glm::ivec2 alt = findAlternateMove(*unit, currentGrid, enemyGrid, reservedCells, triedMoves);
                if (alt == candidate) { // No new candidate found.
                    candidate = currentGrid; // Fallback to holding position.
                    break;
                }
                candidate = alt;
                attempts++;
            }
            finalMoves[unit] = candidate;
            reservedCells[gridKey(candidate.x, candidate.y)] = unit;
        }
    }
    
    return finalMoves;
}
