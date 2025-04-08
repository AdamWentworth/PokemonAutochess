// CameraSystem.cpp

#include "CameraSystem.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

CameraSystem::CameraSystem(Camera3D* cam)
    : camera(cam) {}

void CameraSystem::handleZoom(const SDL_Event& event) {
    if (event.type == SDL_MOUSEWHEEL) {
        camera->zoom(event.wheel.y * 0.5f);
    }
}

void CameraSystem::handleMousePan(const SDL_Event& event) {
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        isPanning = true;
        lastMouseX = event.button.x;
        lastMouseY = event.button.y;
    }

    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
        if (isPanning) {
            isPanning = false;
            std::cout << "[CameraSystem] Pan ended\n";
        }
    }

    if (event.type == SDL_MOUSEMOTION && isPanning) {
        int dx = event.motion.x - lastMouseX;
        int dy = event.motion.y - lastMouseY;
        lastMouseX = event.motion.x;
        lastMouseY = event.motion.y;

        float panSpeed = 0.02f;
        camera->move(glm::vec3(-dx * panSpeed, 0.0f, -dy * panSpeed));
    }
}

void CameraSystem::handleKeyboardMove(const Uint8* keystates) {
    float cameraSpeed = 0.1f;
    glm::vec3 moveDir(0.0f);

    if (keystates[SDL_SCANCODE_W] || keystates[SDL_SCANCODE_UP])    moveDir.z -= cameraSpeed;
    if (keystates[SDL_SCANCODE_S] || keystates[SDL_SCANCODE_DOWN])  moveDir.z += cameraSpeed;
    if (keystates[SDL_SCANCODE_A] || keystates[SDL_SCANCODE_LEFT])  moveDir.x -= cameraSpeed;
    if (keystates[SDL_SCANCODE_D] || keystates[SDL_SCANCODE_RIGHT]) moveDir.x += cameraSpeed;

    if (glm::length(moveDir) > 0.0f) {
        camera->move(moveDir);
    }
}

void CameraSystem::update(float deltaTime) {
    (void)deltaTime; // Not used yet
    handleKeyboardMove(SDL_GetKeyboardState(nullptr));
}
