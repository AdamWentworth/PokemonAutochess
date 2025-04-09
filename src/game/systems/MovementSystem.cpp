// MovementSystem.cpp

#include "MovementSystem.h"
#include <algorithm>
#include <limits>

MovementSystem::MovementSystem(GameWorld* world, std::unordered_map<uint32_t, bool>& gridOccupancy)
    : gameWorld(world), gridOccupancy(gridOccupancy) {}

void MovementSystem::update(float deltaTime) {
    auto& pokemons = gameWorld->getPokemons();
    std::unordered_map<uint32_t, bool> tempGrid;

    // First pass: Calculate desired positions
    for (auto& unit : pokemons) {
        if (!unit.alive) continue;

        glm::ivec2 currentGrid = worldToGrid(unit.position);
        if (!isValidGridPosition(currentGrid.x, currentGrid.y)) continue;

        tempGrid[gridKey(currentGrid.x, currentGrid.y)] = true;
    }

    // Second pass: Perform movement
    for (auto& unit : pokemons) {
        if (!unit.alive) continue;

        glm::ivec2 currentGrid = worldToGrid(unit.position);
        if (!isValidGridPosition(currentGrid.x, currentGrid.y)) continue;

        glm::ivec2 targetGrid = worldToGrid(findNearestEnemyPosition(unit));
        glm::ivec2 moveDir(0);

        // Calculate direction toward target
        if (targetGrid.x != currentGrid.x) {
            moveDir.x = (targetGrid.x > currentGrid.x) ? 1 : -1;
        } else if (targetGrid.y != currentGrid.y) {
            moveDir.y = (targetGrid.y > currentGrid.y) ? 1 : -1;
        }

        glm::ivec2 newGrid = currentGrid;
        bool canMove = false;

        // Try horizontal movement
        if (moveDir.x != 0) {
            glm::ivec2 testGrid = currentGrid;
            testGrid.x += moveDir.x;
            if (isValidGridPosition(testGrid.x, testGrid.y) && !isCellOccupied(testGrid.x, testGrid.y, tempGrid)) {
                newGrid = testGrid;
                canMove = true;
            }
        }

        // Try vertical movement if horizontal blocked
        if (!canMove && moveDir.y != 0) {
            glm::ivec2 testGrid = currentGrid;
            testGrid.y += moveDir.y;
            if (isValidGridPosition(testGrid.x, testGrid.y) && !isCellOccupied(testGrid.x, testGrid.y, tempGrid)) {
                newGrid = testGrid;
                canMove = true;
            }
        }

        if (canMove) {
            // Reserve new grid position
            tempGrid[gridKey(newGrid.x, newGrid.y)] = true;
            tempGrid.erase(gridKey(currentGrid.x, currentGrid.y));

            // Calculate movement parameters
            glm::vec3 targetPos = gridToWorld(newGrid.x, newGrid.y);
            float distanceToMove = unit.movementSpeed * CELL_SIZE * deltaTime;

            // Smooth movement
            glm::vec3 moveDirection = glm::normalize(targetPos - unit.position);
            unit.position += moveDirection * distanceToMove;

            // Snap to grid if close enough
            if (glm::distance(unit.position, targetPos) < 0.1f) {
                unit.position = targetPos;
            }

            // Update rotation
            unit.rotation.y = glm::degrees(atan2f(moveDirection.x, moveDirection.z));
        }
    }

    // Update global occupancy grid
    gridOccupancy = tempGrid;
}

glm::ivec2 MovementSystem::worldToGrid(const glm::vec3& pos) const {
    float boardOriginX = -(GRID_COLS * CELL_SIZE)/2 + CELL_SIZE/2;
    float boardOriginZ = -(GRID_ROWS * CELL_SIZE)/2 + CELL_SIZE/2;
    
    int col = static_cast<int>(std::round((pos.x - boardOriginX) / CELL_SIZE));
    int row = static_cast<int>(std::round((pos.z - boardOriginZ) / CELL_SIZE));
    return {col, row};
}

glm::vec3 MovementSystem::gridToWorld(int col, int row) const {
    float boardOriginX = -(GRID_COLS * CELL_SIZE)/2 + CELL_SIZE/2;
    float boardOriginZ = -(GRID_ROWS * CELL_SIZE)/2 + CELL_SIZE/2;
    
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
    float closestDist = FLT_MAX;
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