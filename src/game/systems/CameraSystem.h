// CameraSystem.h

#pragma once
#include "../../engine/render/Camera3D.h"
#include "../../engine/core/IUpdatable.h"
#include <SDL2/SDL.h>
#include <sol/sol.hpp>

class CameraSystem : public IUpdatable {
public:
    explicit CameraSystem(Camera3D* camera);

    void update(float deltaTime) override;

    // Optional: call if your input loop forwards wheel events here
    void handleZoom(const SDL_Event& event);

    // Event relays (wire these via your EventManager or input layer)
    void onMouseDown(int x, int y);
    void onMouseUp(int x, int y);
    void onMouseMove(int x, int y);
    void onMouseWheel(int wheelY);

private:
    Camera3D* camera;
    sol::state lua;
    bool ok = false;

    void loadScript();
};