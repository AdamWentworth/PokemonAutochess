// CameraSystem.h

#pragma once
#include "././engine/render/Camera3D.h"
#include "././engine/core/IUpdatable.h"
#include <sol/sol.hpp>

struct InputEvent;

class CameraSystem : public IUpdatable {
public:
    explicit CameraSystem(Camera3D* camera);

    void update(float deltaTime) override;

    // Optional: call if your input loop forwards wheel events here
    void handleInput(const InputEvent& event);

    // Event relays (wire these via your EventManager or input layer)
    void onMouseDown(int x, int y, int button);
    void onMouseUp  (int x, int y, int button);
    void onMouseMove(int x, int y);
    void onMouseWheel(int wheelY);

private:
    Camera3D* camera;
    sol::state lua;
    bool ok = false;

    void loadScript();
};


