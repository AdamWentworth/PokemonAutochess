#include "game/GameSession.h"

// Heavy includes live here (not in headers).
#include <iostream>
#include <string>
#include <utility>

#include "engine/core/GameContext.h"
#include "engine/core/EngineServices.h"
#include "engine/core/Paths.h"
#include "engine/input/InputEvent.h"

#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"

#include "engine/ui/UIManager.h"
#include "engine/ui/BattleFeed.h"
#include "engine/ui/HealthBarRenderer.h"

#include "engine/core/SystemRegistry.h"

#include "game/GameWorld.h"
#include "game/GameStateManager.h"
#include "game/GamePreload.h"
#include "game/GameServices.h"
#include "game/GameConfig.h"
#include "game/GameUpdateGraph.h"

#include "game/config/GameDataDb.h"
#include "game/assets/DevAssetStore.h"

#include "game/systems/CameraSystem.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/ShopSystem.h"

#include "game/state/ScriptedState.h"
#include "game/logging/LogBus.h"
#include "game/scripting/ScriptEventBus.h"

namespace game {

struct GameSession::Impl {
    // Pointers (engine-owned)
    Camera3D* camera = nullptr;

    // Owned state
    std::unique_ptr<GameStateManager> stateManager;
    std::unique_ptr<GameWorld>        gameWorld;
    std::unique_ptr<BoardRenderer>    board;
    std::unique_ptr<BattleFeed>       battleFeed;

    // Injected db (owned; loader instances).
    GameDataDb dataDb;

    // Game-owned logger instance (no file-scope globals).
    LogBus::Logger log;
    ScriptEventBus scriptEvents;
    assets::DevAssetStore assetStore;

    // Owned config (loaded once per session).
    GameConfigData config;

    // v1: thread config/db/log into states without singletons.
    std::unique_ptr<GameServices> services;

    HealthBarRenderer healthBarRenderer;
    SystemRegistry systemRegistry;
    GameUpdateGraph updateGraph;

    bool renderEnabled = false;

    std::shared_ptr<CameraSystem>           cameraSystem;
    std::shared_ptr<UnitInteractionSystem>  unitSystem;
    std::shared_ptr<ShopSystem>             shopSystem;
    std::shared_ptr<RoundSystem>            roundSystem;


    Impl(GameContext& ctx, GameDataDb db) : dataDb(std::move(db)) { init(ctx); }

    void init(GameContext& ctx) {
        camera = ctx.camera;
        renderEnabled = (ctx.renderer != nullptr) && (ctx.camera != nullptr);

        assetStore.setRoot(engine::paths::dataRoot());
        config = GameConfig::load(&log);
        services = std::make_unique<GameServices>(config, dataDb, log, scriptEvents, assetStore);

        // Board visuals
        if (renderEnabled) {
            board = std::make_unique<BoardRenderer>(config.rows, config.cols, config.cellSize);
        }

        // World
        gameWorld = std::make_unique<GameWorld>();
        gameWorld->setLogger(&log);
        if (ctx.services) gameWorld->setResources(ctx.services->resources);
        gameWorld->setData(&dataDb);
        gameWorld->setConfig(&config);

        // State stack
        stateManager = std::make_unique<GameStateManager>();

        // Systems
        if (camera) {
            cameraSystem = std::make_shared<CameraSystem>(camera);
            unitSystem   = std::make_shared<UnitInteractionSystem>(camera, gameWorld.get(), ctx.drawableW, ctx.drawableH);
        }
        roundSystem  = std::make_shared<RoundSystem>();
        shopSystem   = std::make_shared<ShopSystem>();

        if (cameraSystem) systemRegistry.registerSystem(cameraSystem);
        if (unitSystem)   systemRegistry.registerSystem(unitSystem);
        if (roundSystem)  systemRegistry.registerSystem(roundSystem);
        if (shopSystem)   systemRegistry.registerSystem(shopSystem);

        if (renderEnabled) {
            if (ctx.services && ctx.services->shaders) {
                healthBarRenderer.init(*ctx.services->shaders);
            } else {
                healthBarRenderer.init();
            }

            // Battle feed + logger (instance-based)
            battleFeed = std::make_unique<BattleFeed>(config.fontPath, config.fontSize);
            log.attach(battleFeed.get());
            log.setEchoToStdout(false);
        }

        updateGraph.configure({
            &systemRegistry,
            roundSystem.get(),
            shopSystem.get(),
            stateManager.get(),
            gameWorld.get(),
            battleFeed.get(),
            &log,
            &scriptEvents
        });

        // Preload common models (uses the db's pokemon loader).
        if (renderEnabled) {
            game::preload::preloadCommonModels(ctx, dataDb.pokemon, "PokemonAutochess");
        } else {
            std::cout << "[Init] Headless mode (renderer/camera missing): skipping model preload.\n";
        }

        stateManager->pushState(std::make_unique<ScriptedState>(
            stateManager.get(),
            gameWorld.get(),
            *services,
            engine::paths::data("scripts/states/starter.lua")
        ));

        if (ctx.setTitle) ctx.setTitle("Pokemon Autochess");
        std::cout << "[Init] Game initialized.\n";
    }

    void handleEvent(const InputEvent& event) {
        if (cameraSystem) cameraSystem->handleInput(event);
        if (unitSystem)   unitSystem->handleInput(event);
        if (shopSystem)   shopSystem->handleInput(event);
        if (stateManager) stateManager->handleInput(event);
    }

    void fixedUpdate(float dt) {
        updateGraph.tick(dt);
    }

    void render(int drawableW, int drawableH) {
        if (!renderEnabled) return;
        if (board && camera) board->draw(*camera);
        if (gameWorld && camera && board) gameWorld->drawAll(*camera, *board);
        if (stateManager) stateManager->render();

        if (gameWorld && camera) {
            auto healthBarData = gameWorld->getHealthBarData(*camera, drawableW, drawableH);
            healthBarRenderer.render(healthBarData);
        }

        if (shopSystem) shopSystem->renderUI(drawableW, drawableH);
        if (battleFeed) battleFeed->render(drawableW, drawableH);
    }

    void shutdown() {
        std::cout << "[Shutdown] Game.\n";

        log.attach(nullptr);

        if (board) {
            board->shutdown();
            board.reset();
        }

        if (renderEnabled) {
            UIManager::shutdown();
        }

        battleFeed.reset();
        shopSystem.reset();
        unitSystem.reset();
        cameraSystem.reset();
        roundSystem.reset();

        stateManager.reset();
        gameWorld.reset();

        systemRegistry.clear();

        std::cout << "[Shutdown] Game done.\n";
    }
};

GameSession::GameSession(GameContext& ctx, GameDataDb db)
    : impl_(std::make_unique<Impl>(ctx, std::move(db))) {}

GameSession::~GameSession() = default;

GameSession::GameSession(GameSession&&) noexcept = default;
GameSession& GameSession::operator=(GameSession&&) noexcept = default;

void GameSession::handleEvent(const InputEvent& event) { impl_->handleEvent(event); }
void GameSession::fixedUpdate(float dt) { impl_->fixedUpdate(dt); }
void GameSession::render(int drawableW, int drawableH) { impl_->render(drawableW, drawableH); }
void GameSession::shutdown() { impl_->shutdown(); }

} // namespace game
