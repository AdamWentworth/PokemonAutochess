// Application.h

#pragma once

#include "SystemRegistry.h"
#include "../render/Renderer.h"
#include "../render/Camera3D.h"
#include "../ui/HealthBarRenderer.h"
#include "../../game/GameStateManager.h"
#include "../render/BoardRenderer.h"

class GameWorld;
class Window;
class CameraSystem;
class UnitInteractionSystem;
class ShopSystem;              // ← NEW

class Application {
public:
    Application();
    ~Application();
    void run();

private:
    void init();
    void update();
    void shutdown();

    static constexpr float TIME_STEP = 1.0f / 60.0f;

    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Camera3D> camera;
    std::unique_ptr<GameStateManager> stateManager;
    std::unique_ptr<GameWorld> gameWorld;
    std::unique_ptr<Window> window;
    std::unique_ptr<BoardRenderer> board;
    HealthBarRenderer healthBarRenderer;

    std::shared_ptr<CameraSystem> cameraSystem;
    std::shared_ptr<UnitInteractionSystem> unitSystem;
    std::shared_ptr<ShopSystem> shopSystem;   // ← NEW
};
