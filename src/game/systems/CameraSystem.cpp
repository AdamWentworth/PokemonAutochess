// CameraSystem.cpp

#include "CameraSystem.h"
#include "../../engine/events/EventManager.h"   // Include our event manager
#include "../../engine/events/Event.h"          // For specialized events
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

CameraSystem::CameraSystem(Camera3D* cam)
    : camera(cam)
{
    // Subscribe to MouseButtonDownEvent: start panning.
    EventManager::getInstance().subscribe(EventType::MouseButtonDown, 
        [this](const Event& event) {
            const auto& mbEvent = static_cast<const MouseButtonDownEvent&>(event);
            isPanning = true;
            lastMouseX = mbEvent.getX();
            lastMouseY = mbEvent.getY();
        }
    );

    // Subscribe to MouseButtonUpEvent: end panning.
    EventManager::getInstance().subscribe(EventType::MouseButtonUp, 
        [this](const Event& event) {
            if (isPanning) {
                isPanning = false;
                std::cout << "[CameraSystem] Pan ended\n";
            }
        }
    );

    // Subscribe to MouseMotionEvent: update camera if panning.
    EventManager::getInstance().subscribe(EventType::MouseMoved, 
        [this](const Event& event) {
            const auto& mmEvent = static_cast<const MouseMotionEvent&>(event);
            if (isPanning) {
                int dx = mmEvent.getX() - lastMouseX;
                int dy = mmEvent.getY() - lastMouseY;
                lastMouseX = mmEvent.getX();
                lastMouseY = mmEvent.getY();
                float panSpeed = 0.02f;
                camera->move(glm::vec3(-dx * panSpeed, 0.0f, -dy * panSpeed));
            }
        }
    );
}

void CameraSystem::handleZoom(const SDL_Event& event) {
    if (event.type == SDL_MOUSEWHEEL) {
        camera->zoom(event.wheel.y * 0.5f);
    }
}

void CameraSystem::handleKeyboardMove(const Uint8* keystates) {
    float cameraSpeed = 0.1f;
    glm::vec3 moveDir(0.0f);
    if (keystates[SDL_SCANCODE_W] || keystates[SDL_SCANCODE_UP])    moveDir.z -= cameraSpeed;
    if (keystates[SDL_SCANCODE_S] || keystates[SDL_SCANCODE_DOWN])  moveDir.z += cameraSpeed;
    if (keystates[SDL_SCANCODE_A] || keystates[SDL_SCANCODE_LEFT])  moveDir.x -= cameraSpeed;
    if (keystates[SDL_SCANCODE_D] || keystates[SDL_SCANCODE_RIGHT]) moveDir.x += cameraSpeed;
    if (glm::length(moveDir) > 0.0f)
        camera->move(moveDir);
}

void CameraSystem::update(float deltaTime) {
    (void)deltaTime; // Not used yet
    handleKeyboardMove(SDL_GetKeyboardState(nullptr));
}
