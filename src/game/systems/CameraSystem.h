// CameraSystem.h

#pragma once

#include "../../engine/render/Camera3D.h"
#include "../../engine/core/IUpdatable.h" // ✅ Add this include
#include <SDL2/SDL.h>

class CameraSystem : public IUpdatable { // ✅ Inherit from IUpdatable
public:
    CameraSystem(Camera3D* camera);

    void handleMousePan(const SDL_Event& event);
    void handleKeyboardMove(const Uint8* keystates);
    void handleZoom(const SDL_Event& event);

    void update(float deltaTime) override; // ✅ Implement IUpdatable interface

private:
    Camera3D* camera;
    bool isPanning = false;
    int lastMouseX = 0;
    int lastMouseY = 0;
};