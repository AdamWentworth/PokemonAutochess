#include "game/GameSession.h"

// Heavy includes live here (not in headers).
#include <iostream>
#include <string>

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

#include "game/config/PokemonConfigLoader.h"
#include "game/config/MovesConfigLoader.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/FlyerConfigLoader.h"
#include "game/config/GameDataDb.h"

#include "game/systems/CameraSystem.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/ShopSystem.h"

#include "game/state/ScriptedState.h"
#include "game/logging/LogBus.h"

namespace game_session_detail {
static const char* phaseName(RoundPhase p) {
    switch (p) {
        case RoundPhase::Planning:   return "Planning";
        case RoundPhase::Battle:     return "Battle";
        case RoundPhase::Resolution: return "Resolution";
        default:                     return "Unknown";
    }
}
} // namespace game_session_detail

namespace game {

struct GameSession::Impl {
    // Pointers (engine-owned)
    Camera3D* camera = nullptr;

    // Owned state
    std::unique_ptr<GameStateManager> stateManager;
    std::unique_ptr<GameWorld>        gameWorld;
    std::unique_ptr<BoardRenderer>    board;
    std::unique_ptr<BattleFeed>       battleFeed;

    // Threaded into GameWorld to avoid gameplay code calling config singletons.
    GameDataDb dataDb;

    // Game-owned logger instance (no file-scope globals).
    LogBus::Logger log;

    // v1: thread config/db/log into states without singletons.
    std::unique_ptr<GameServices> services;

    HealthBarRenderer healthBarRenderer;
    SystemRegistry systemRegistry;

    std::shared_ptr<CameraSystem>           cameraSystem;
    std::shared_ptr<UnitInteractionSystem>  unitSystem;
    std::shared_ptr<ShopSystem>             shopSystem;
    std::shared_ptr<RoundSystem>            roundSystem;

    RoundPhase lastRoundPhase = RoundPhase::Planning;
    bool hasLastRoundPhase = false;

    explicit Impl(GameContext& ctx) { init(ctx); }

    void init(GameContext& ctx) {
        // Wire db pointers to already-loaded singleton loaders (loaded by GameBootstrap).
        dataDb.pokemon      = &PokemonConfigLoader::getInstance();
        dataDb.moves        = &MovesConfigLoader::getInstance();
        dataDb.attackAnims  = &AttackAnimConfigLoader::getInstance();
        dataDb.flyers       = &FlyerConfigLoader::getInstance();

        camera = ctx.camera;

        const auto& cfg = GameConfig::get();
        services = std::make_unique<GameServices>(cfg, dataDb, log);

        // Board visuals
        board = std::make_unique<BoardRenderer>(cfg.rows, cfg.cols, cfg.cellSize);

        // World
        gameWorld = std::make_unique<GameWorld>();
        if (ctx.services) gameWorld->setResources(ctx.services->resources);
        gameWorld->setData(&dataDb);

        // State stack
        stateManager = std::make_unique<GameStateManager>();

        // Systems
        cameraSystem = std::make_shared<CameraSystem>(camera);
        unitSystem   = std::make_shared<UnitInteractionSystem>(camera, gameWorld.get(), ctx.drawableW, ctx.drawableH);
        roundSystem  = std::make_shared<RoundSystem>();
        shopSystem   = std::make_shared<ShopSystem>();

        systemRegistry.registerSystem(cameraSystem);
        systemRegistry.registerSystem(unitSystem);
        systemRegistry.registerSystem(roundSystem);
        systemRegistry.registerSystem(shopSystem);

        if (roundSystem && shopSystem) {
            lastRoundPhase = roundSystem->getCurrentPhase();
            hasLastRoundPhase = true;
            shopSystem->onRoundPhaseChanged(lastRoundPhase, lastRoundPhase);
        }

        healthBarRenderer.init();

        // Battle feed + logger (instance-based)
        battleFeed = std::make_unique<BattleFeed>(cfg.fontPath, cfg.fontSize);
        log.attach(battleFeed.get());
        log.setEchoToStdout(false);
        LogBus::setActive(&log);

        // Preload common models (uses the same PokemonConfigLoader instance Lua/gameplay sees).
        game::preload::preloadCommonModels(ctx, *dataDb.pokemon, "PokemonAutochess");

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
        systemRegistry.updateAll(dt);

        if (roundSystem && shopSystem) {
            const RoundPhase current = roundSystem->getCurrentPhase();
            if (!hasLastRoundPhase) {
                lastRoundPhase = current;
                hasLastRoundPhase = true;
            } else if (current != lastRoundPhase) {
                shopSystem->onRoundPhaseChanged(lastRoundPhase, current);

                LogBus::colored(
                    std::string("Phase: ") + game_session_detail::phaseName(lastRoundPhase) +
                        " \xE2\x86\x92 " + game_session_detail::phaseName(current),
                    {0.75f, 0.9f, 1.0f},
                    3.0f
                );

                lastRoundPhase = current;
            }
        }

        if (stateManager) stateManager->update(dt);
        if (gameWorld)    gameWorld->update(dt);
        if (battleFeed)   battleFeed->update(dt);
    }

    void render(int drawableW, int drawableH) {
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

        LogBus::setActive(nullptr);
        log.attach(nullptr);

        if (board) {
            board->shutdown();
            board.reset();
        }

        UIManager::shutdown();

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

GameSession::GameSession(GameContext& ctx)
    : impl_(std::make_unique<Impl>(ctx)) {}

GameSession::~GameSession() = default;

GameSession::GameSession(GameSession&&) noexcept = default;
GameSession& GameSession::operator=(GameSession&&) noexcept = default;

void GameSession::handleEvent(const InputEvent& event) { impl_->handleEvent(event); }
void GameSession::fixedUpdate(float dt) { impl_->fixedUpdate(dt); }
void GameSession::render(int drawableW, int drawableH) { impl_->render(drawableW, drawableH); }
void GameSession::shutdown() { impl_->shutdown(); }

} // namespace game
