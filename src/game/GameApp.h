// GameApp.h
#pragma once

#include <memory>

#include "../engine/core/GameLoop.h"
#include "../engine/core/SystemRegistry.h"
#include "../engine/ui/HealthBarRenderer.h"

// Forward decls (engine)
class Application;
class Camera3D;
class BoardRenderer;
class BattleFeed;
class CameraSystem;
class UnitInteractionSystem;
class ShopSystem;

// Forward decls (game)
class GameWorld;
class GameStateManager;

/*
    GameApp (Game):
    - Owns game-specific initialization, world, state machine, systems, and game UI.
    - Implements GameLoop so the Engine can call it without including game headers.
    IMPORTANT (MSVC):
    - Constructor + destructor are out-of-line so std::unique_ptr can hold forward-declared types.
*/
class GameApp final : public GameLoop {
public:
    GameApp();
    ~GameApp() override;

    void init(Application& app) override;
    void handleEvent(SDL_Event& event) override;

    void fixedUpdate(float dt) override;
    void render(int drawableW, int drawableH) override;

    void shutdown() override;

private:
    void preloadCommonModels(Application& app);

private:
    Camera3D* camera = nullptr;

    std::unique_ptr<GameStateManager> stateManager;
    std::unique_ptr<GameWorld>        gameWorld;
    std::unique_ptr<BoardRenderer>    board;
    std::unique_ptr<BattleFeed>       battleFeed;

    HealthBarRenderer healthBarRenderer;

    std::shared_ptr<CameraSystem>          cameraSystem;
    std::shared_ptr<UnitInteractionSystem> unitSystem;
    std::shared_ptr<ShopSystem>            shopSystem;
};
