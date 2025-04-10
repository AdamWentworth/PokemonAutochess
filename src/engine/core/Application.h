// Application.h

#pragma once

#include "SystemRegistry.h"
#include "../render/Renderer.h"
#include "../render/Camera3D.h"
#include "../ui/HealthBarRenderer.h"
#include "../../game/GameStateManager.h"
#include <memory>

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
    HealthBarRenderer healthBarRenderer;

    std::shared_ptr<CameraSystem> cameraSystem;
    std::shared_ptr<UnitInteractionSystem> unitSystem;
};
