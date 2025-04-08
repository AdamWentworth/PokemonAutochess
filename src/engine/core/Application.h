// Application.h

#pragma once

#include "../render/Renderer.h"
#include "../render/Camera3D.h"
#include "../../game/GameStateManager.h"

class GameWorld;
class Window;
class CameraSystem;
class UnitInteractionSystem;

class Application {
public:
    Application();
    ~Application();
    void run();

private:
    void init();
    void update();
    void shutdown();

    Renderer* renderer = nullptr;
    Camera3D* camera = nullptr;
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    Window* window = nullptr;

    CameraSystem* cameraSystem = nullptr;
    UnitInteractionSystem* unitSystem = nullptr;
};
