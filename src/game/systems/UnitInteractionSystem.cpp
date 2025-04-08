// UnitInteractionSystem.cpp

#include "UnitInteractionSystem.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <glm/common.hpp> // for glm::clamp

UnitInteractionSystem::UnitInteractionSystem(Camera3D* cam, GameWorld* world, unsigned int w, unsigned int h)
    : camera(cam), gameWorld(world), screenW(w), screenH(h)
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
    float benchZ = (8 * cellSize) / 2.0f + 0.5f;
    float benchZEnd = benchZ + cellSize;
    return pos.z >= benchZ && pos.z <= benchZEnd;
}

bool UnitInteractionSystem::isInBoardZone(const glm::vec3& pos) const {
    float halfBoard = (8 * cellSize) / 2.0f;
    return pos.z >= -halfBoard && pos.z <= halfBoard;
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
                float dist = glm::distance(clickPos, pokemons[i].position);
                if (dist < closestDist) {
                    closestDist = dist;
                    closestIdx = i;
                    draggingFromBench = false;
                }
            }

            auto& bench = gameWorld->getBenchPokemons();
            for (int i = 0; i < (int)bench.size(); ++i) {
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
        } else {
            glm::vec3 rawDropPos = screenToWorld(event.button.x, event.button.y);
            rawDropPos.y = 0.0f;

            bool dropToBench = isInBenchZone(rawDropPos);
            bool dropToBoard = isInBoardZone(rawDropPos);

            glm::vec3 snappedDrop = rawDropPos;
            float boardHalf = (8 * cellSize) / 2.0f - cellSize * 0.5f;

            if (dropToBench) {
                int slot = (int)std::round((rawDropPos.x + (4 * cellSize)) / cellSize);
                slot = glm::clamp(slot, 0, 7);
                float x = (slot - 4) * cellSize + cellSize * 0.5f;
                float z = (8 * cellSize) / 2.0f + 0.5f + cellSize * 0.5f;
                snappedDrop = glm::vec3(x, 0.0f, z);
            } else if (dropToBoard) {
                snappedDrop.x = std::floor(rawDropPos.x / cellSize) * cellSize + cellSize * 0.5f;
                snappedDrop.z = std::floor(rawDropPos.z / cellSize) * cellSize + cellSize * 0.5f;
                snappedDrop.y = 0.0f;

                snappedDrop.x = glm::clamp(snappedDrop.x, -boardHalf, boardHalf);
                snappedDrop.z = glm::clamp(snappedDrop.z, -boardHalf, boardHalf);
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

        float boardHalf = (8 * cellSize) / 2.0f - cellSize * 0.5f;

        if (isInBenchZone(rawPos)) {
            int slot = (int)std::round((rawPos.x + (4 * cellSize)) / cellSize);
            slot = glm::clamp(slot, 0, 7);
            float x = (slot - 4) * cellSize + cellSize * 0.5f;
            float z = (8 * cellSize) / 2.0f + 0.5f + cellSize * 0.5f;
            snappedPos = glm::vec3(x, 0.0f, z);
        } else {
            snappedPos.x = std::floor(rawPos.x / cellSize) * cellSize + cellSize * 0.5f;
            snappedPos.z = std::floor(rawPos.z / cellSize) * cellSize + cellSize * 0.5f;
            snappedPos.y = 0.0f;

            snappedPos.x = glm::clamp(snappedPos.x, -boardHalf, boardHalf);
            snappedPos.z = glm::clamp(snappedPos.z, -boardHalf, boardHalf);
        }

        if (draggingFromBench) {
            gameWorld->getBenchPokemons()[draggedIndex].position = snappedPos;
        } else {
            gameWorld->getPokemons()[draggedIndex].position = snappedPos;
        }
    }
}

void UnitInteractionSystem::update(float deltaTime) {
    (void)deltaTime;
}