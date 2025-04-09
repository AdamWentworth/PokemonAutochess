// UnitInteractionSystem.cpp

#include "UnitInteractionSystem.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/common.hpp>
#include <iostream>
#include <limits>

UnitInteractionSystem::UnitInteractionSystem(Camera3D* cam, GameWorld* world, unsigned int w, unsigned int h)
    : camera(cam), gameWorld(world), screenW(w), screenH(h), benchSystem(1.2f)
{
    cellSize = 1.2f;
}

glm::vec3 UnitInteractionSystem::screenToWorld(int mouseX, int mouseY) const {
    glm::vec4 viewport(0.0f, 0.0f, screenW, screenH);
    float winX = static_cast<float>(mouseX);
    float winY = static_cast<float>(screenH - mouseY);

    glm::vec3 near = glm::unProject(glm::vec3(winX, winY, 0.0f),
                                    camera->getViewMatrix(), camera->getProjectionMatrix(), viewport);
    glm::vec3 far = glm::unProject(glm::vec3(winX, winY, 1.0f),
                                   camera->getViewMatrix(), camera->getProjectionMatrix(), viewport);
    glm::vec3 dir = glm::normalize(far - near);
    float t = -near.y / dir.y;
    return near + t * dir;
}

bool UnitInteractionSystem::isInBenchZone(const glm::vec3& pos) const {
    return benchSystem.isInBenchZone(pos);
}

bool UnitInteractionSystem::isInBoardZone(const glm::vec3& pos) const {
    return pos.z >= (cellSize * 0.5f) && pos.z <= (cellSize * 3.5f);
}

void UnitInteractionSystem::handleEvent(const SDL_Event& event) {
    if (!gameWorld) return;

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        glm::vec3 clickPos = screenToWorld(event.button.x, event.button.y);

        if (!draggingUnit) {
            float closestDist = std::numeric_limits<float>::max();
            int closestIdx = -1;
            draggingFromBench = false;

            auto& pokemons = gameWorld->getPokemons();
            for (int i = 0; i < (int)pokemons.size(); ++i) {
                // Skip enemy units
                if (pokemons[i].side != PokemonSide::Player) {
                    continue;
                }
                float dist = glm::distance(clickPos, pokemons[i].position);
                if (dist < closestDist) {
                    closestDist = dist;
                    closestIdx = i;
                    draggingFromBench = false;
                }
            }

            auto& bench = gameWorld->getBenchPokemons();
            for (int i = 0; i < (int)bench.size(); ++i) {
                // Bench should only have player units, but check anyway
                if (bench[i].side != PokemonSide::Player) {
                    continue;
                }
                float dist = glm::distance(clickPos, bench[i].position);
                if (dist < closestDist) {
                    closestDist = dist;
                    closestIdx = i;
                    draggingFromBench = true;
                }
            }

            if (closestIdx >= 0 && closestDist <= pickRadius) {
                draggingUnit = true;
                draggedIndex = closestIdx;
                std::cout << "[UnitInteraction] Picked up index " << draggedIndex
                          << (draggingFromBench ? " from bench\n" : " from board\n");
            }
        }
        else {
            glm::vec3 rawDropPos = screenToWorld(event.button.x, event.button.y);
            rawDropPos.y = 0.0f;

            bool dropToBench = isInBenchZone(rawDropPos);
            bool dropToBoard = isInBoardZone(rawDropPos);

            glm::vec3 snappedDrop = rawDropPos;
            if (dropToBench) {
                snappedDrop = benchSystem.getSnappedBenchPosition(rawDropPos);
            }
            else if (dropToBoard) {
                // Define the board origin so that the snapping aligns with the center of the grid slots.
                float boardOriginX = -((8 * cellSize) / 2.0f) + cellSize * 0.5f;
                float boardOriginZ = cellSize * 0.5f;  // first board row center
                
                // Compute column and row indices by comparing against the board origin.
                int col = static_cast<int>(std::round((rawDropPos.x - boardOriginX) / cellSize));
                int row = static_cast<int>(std::round((rawDropPos.z - boardOriginZ) / cellSize));
                
                // Clamp indices to valid board ranges (8 columns and 4 rows for the board).
                col = glm::clamp(col, 0, 7);
                row = glm::clamp(row, 0, 3);
                
                // Reconstruct the snapped drop position.
                snappedDrop.x = boardOriginX + col * cellSize;
                snappedDrop.z = boardOriginZ + row * cellSize;
                snappedDrop.y = 0.0f;
            }            

            if (dropToBench && !draggingFromBench) {
                auto& pokemons = gameWorld->getPokemons();
                auto unit = pokemons[draggedIndex];
                pokemons.erase(pokemons.begin() + draggedIndex);

                unit.position = snappedDrop;
                gameWorld->getBenchPokemons().push_back(unit);
                std::cout << "[UnitInteraction] Moved unit to bench\n";
            }
            else if (dropToBoard && draggingFromBench) {
                auto& bench = gameWorld->getBenchPokemons();
                auto unit = bench[draggedIndex];
                bench.erase(bench.begin() + draggedIndex);

                unit.position = snappedDrop;
                gameWorld->getPokemons().push_back(unit);
                std::cout << "[UnitInteraction] Moved unit to board\n";
            }

            draggingUnit = false;
            draggedIndex = -1;
            draggingFromBench = false;
        }
    }

    if (event.type == SDL_MOUSEMOTION && draggingUnit && draggedIndex >= 0) {
        glm::vec3 rawPos = screenToWorld(event.motion.x, event.motion.y);
        glm::vec3 snappedPos = rawPos;

        if (isInBenchZone(rawPos)) {
            snappedPos = benchSystem.getSnappedBenchPosition(rawPos);
        } else {
            float boardOriginX = -((8 * cellSize) / 2.0f) + cellSize * 0.5f;
            float boardOriginZ = cellSize * 0.5f;
            
            int col = static_cast<int>(std::round((rawPos.x - boardOriginX) / cellSize));
            int row = static_cast<int>(std::round((rawPos.z - boardOriginZ) / cellSize));
            
            // Clamp to valid cell indices
            col = glm::clamp(col, 0, 7);
            row = glm::clamp(row, 0, 3);
            
            snappedPos.x = boardOriginX + col * cellSize;
            snappedPos.z = boardOriginZ + row * cellSize;
            snappedPos.y = 0.0f;
        }        

        if (draggingFromBench) {
            gameWorld->getBenchPokemons()[draggedIndex].position = snappedPos;
        }
        else {
            gameWorld->getPokemons()[draggedIndex].position = snappedPos;
        }
    }
}

void UnitInteractionSystem::update(float deltaTime) {
    (void)deltaTime;
}
