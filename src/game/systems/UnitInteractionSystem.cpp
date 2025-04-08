// UnitInteractionSystem.cpp

#include "UnitInteractionSystem.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>

UnitInteractionSystem::UnitInteractionSystem(Camera3D* cam, GameWorld* world, unsigned int w, unsigned int h)
    : camera(cam), gameWorld(world), screenW(w), screenH(h) {}

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

void UnitInteractionSystem::handleEvent(const SDL_Event& event) {
    if (!gameWorld) return;

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        glm::vec3 clickPos = screenToWorld(event.button.x, event.button.y);

        if (!draggingUnit) {
            float closestDist = std::numeric_limits<float>::max();
            int closestIdx = -1;

            auto& pokemons = gameWorld->getPokemons();
            for (int i = 0; i < (int)pokemons.size(); ++i) {
                float dist = glm::distance(clickPos, pokemons[i].position);
                if (dist < closestDist) {
                    closestDist = dist;
                    closestIdx = i;
                }
            }

            if (closestIdx >= 0 && closestDist <= pickRadius) {
                draggingUnit = true;
                draggedIndex = closestIdx;
                std::cout << "[UnitInteraction] Picked up index " << closestIdx << "\n";
            }
        } else {
            draggingUnit = false;
            draggedIndex = -1;
            std::cout << "[UnitInteraction] Dropped unit\n";
        }
    }

    if (event.type == SDL_MOUSEMOTION && draggingUnit && draggedIndex >= 0) {
        glm::vec3 newPos = screenToWorld(event.motion.x, event.motion.y);
        newPos.x = std::floor(newPos.x) + 0.5f;
        newPos.z = std::floor(newPos.z) + 0.5f;
        newPos.y = 0.0f;
        newPos.x = glm::clamp(newPos.x, -3.5f, 3.5f);
        newPos.z = glm::clamp(newPos.z, 0.5f, 3.5f);
        gameWorld->getPokemons()[draggedIndex].position = newPos;
    }
}

void UnitInteractionSystem::update(float deltaTime) {
    // For now, this might be empty, but later you could:
    // - Show hovered unit
    // - Trigger passive effects
    // - Poll for interaction cooldowns, etc.
    (void)deltaTime; // silence unused warning
}
