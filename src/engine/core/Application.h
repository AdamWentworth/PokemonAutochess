// Application.h
#pragma once

#include "SystemRegistry.h"
#include "../render/Renderer.h"
#include "../render/Camera3D.h"
#include "../ui/HealthBarRenderer.h"
#include "../../game/GameStateManager.h"
#include "../render/BoardRenderer.h"
#include "../ui/BattleFeed.h"

class GameWorld;
class Window;
class CameraSystem;
class UnitInteractionSystem;
class ShopSystem;

class Application {
public:
    Application();
    ~Application();
    void run();

private:
    void init();
    void update();
    void shutdown();

    void updateDrawableSizeAndViewport();   // NEW
    void updateMouseScale();                // NEW
    void preloadCommonModels();             // NEW

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
    std::shared_ptr<ShopSystem> shopSystem;
    std::unique_ptr<BattleFeed> battleFeed;

    // NEW: real render dimensions (framebuffer / drawable)
    int drawableW = 1280;
    int drawableH = 720;

    // NEW: window dimensions (logical)
    int windowW = 1280;
    int windowH = 720;

    // NEW: scale SDL mouse coords -> drawable coords
    float mouseScaleX = 1.0f;
    float mouseScaleY = 1.0f;
};
