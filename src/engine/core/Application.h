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

    // Changed raw pointers to smart pointers.
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Camera3D> camera;
    std::unique_ptr<GameStateManager> stateManager;
    std::unique_ptr<GameWorld> gameWorld;
    std::unique_ptr<Window> window;
    HealthBarRenderer healthBarRenderer;

    std::shared_ptr<CameraSystem> cameraSystem;
    std::shared_ptr<UnitInteractionSystem> unitSystem;
};
