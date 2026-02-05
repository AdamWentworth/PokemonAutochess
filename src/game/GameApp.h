// src/game/GameApp.h
#pragma once

#include <memory>

#include "engine/core/GameLoop.h"
#include "engine/core/SystemRegistry.h"
#include "engine/ui/HealthBarRenderer.h"

#include "game/systems/RoundSystem.h" // for RoundPhase
#include "game/logging/LogBus.h"      // for LogBus::Logger
#include "game/config/GameDataDb.h"

// Forward decls (engine)
struct GameContext;
struct InputEvent;
class Camera3D;
class BoardRenderer;
class BattleFeed;

// Forward decls (game)
class GameWorld;
class GameStateManager;
class CameraSystem;
class UnitInteractionSystem;
class ShopSystem;

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

    void init(GameContext& ctx) override;
    void handleEvent(const InputEvent& event) override;

    void fixedUpdate(float dt) override;
    void render(int drawableW, int drawableH) override;

    void shutdown() override;

private:
    void preloadCommonModels(GameContext& ctx);

private:
    Camera3D* camera = nullptr;

    std::unique_ptr<GameStateManager> stateManager;
    std::unique_ptr<GameWorld>        gameWorld;
    std::unique_ptr<BoardRenderer>    board;
    std::unique_ptr<BattleFeed>       battleFeed;

    // Threaded into GameWorld to avoid gameplay code calling config singletons.
    GameDataDb dataDb;

    // Game-owned logger instance (no file-scope globals).
    LogBus::Logger log;

    HealthBarRenderer healthBarRenderer;

    SystemRegistry systemRegistry;

    std::shared_ptr<CameraSystem>          cameraSystem;
    std::shared_ptr<UnitInteractionSystem> unitSystem;
    std::shared_ptr<ShopSystem>            shopSystem;

    // Keep a direct handle to RoundSystem so we can react to phase changes without a global event bus.
    std::shared_ptr<RoundSystem> roundSystem;
    RoundPhase lastRoundPhase = RoundPhase::Planning;
    bool hasLastRoundPhase = false;
};