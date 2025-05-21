// MovementSystem.cpp

#include "MovementSystem.h"
#include "movement/MovementPlanner.h"   // For planning moves
#include "movement/MovementExecutor.h"  // For executing moves
#include <algorithm>
#include <limits>
#include <vector>
#include <queue>
#include <cmath>
#include <iostream>

#define LOG(x) std::cout << "[MovementSystem] " << x << std::endl;

MovementSystem::MovementSystem(GameWorld* world,
                               std::unordered_map<uint32_t,bool>& gridOccupancy)
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

void MovementSystem::update(float deltaTime) {
    // 1. Use the MovementPlanner to generate desired moves.
    MovementPlanner planner(gameWorld, GRID_COLS, GRID_ROWS, CELL_SIZE);
    std::unordered_map<PokemonInstance*, glm::ivec2> plannedMoves = planner.planMoves();

    // 2. Use the MovementExecutor to execute moves.
    MovementExecutor executor(gameWorld, GRID_COLS, GRID_ROWS, CELL_SIZE);
    std::unordered_map<uint32_t, bool> tempGrid = executor.executeMoves(plannedMoves, deltaTime);

    // 3. Update unit rotations to face the nearest enemy.
    executor.updateUnitRotations();

    // 4. Update the grid occupancy.
    gridOccupancy = tempGrid;
}