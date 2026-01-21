// src/game/GameApp.cpp

#include "GameApp.h"

#include "game/input/SDLInputAdapter.h"

#include "engine/events/Event.h"
#include "engine/events/EventManager.h"
#include "engine/events/RoundEvents.h"

#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"
#include "engine/utils/ResourceManager.h"

#include "engine/ui/UIManager.h"
#include "engine/ui/BattleFeed.h"

#include "game/GameWorld.h"
#include "game/GameStateManager.h"
#include "game/PokemonConfigLoader.h"
#include "game/MovesConfigLoader.h"
#include "game/systems/CameraSystem.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/ShopSystem.h"
#include "game/ScriptedState.h"
#include "game/GameConfig.h"
#include "game/LogBus.h"

#include <SDL2/SDL.h>

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

GameApp::GameApp() = default;
GameApp::~GameApp() = default;

void GameApp::init(GameContext& inCtx) {
    ctx = &inCtx;

    PokemonConfigLoader::getInstance().loadConfig("config/pokemon_config.json");
    MovesConfigLoader::getInstance().loadConfig("config/moves_config.json");

    std::cout << "[Init] CWD: " << std::filesystem::current_path() << "\n";

    camera = ctx->camera;

    const auto& cfg = GameConfig::get();
    board = std::make_unique<BoardRenderer>(cfg.rows, cfg.cols, cfg.cellSize);

    gameWorld    = std::make_unique<GameWorld>();
    stateManager = std::make_unique<GameStateManager>();

    cameraSystem = std::make_shared<CameraSystem>(camera);
    unitSystem   = std::make_shared<UnitInteractionSystem>(
        camera, gameWorld.get(), ctx->drawableW, ctx->drawableH
    );

    SystemRegistry::getInstance().registerSystem(cameraSystem);
    SystemRegistry::getInstance().registerSystem(unitSystem);

    auto roundSystem = std::make_shared<RoundSystem>();
    SystemRegistry::getInstance().registerSystem(roundSystem);

    shopSystem = std::make_shared<ShopSystem>();
    SystemRegistry::getInstance().registerSystem(shopSystem);

    healthBarRenderer.init();

    battleFeed = std::make_unique<BattleFeed>(cfg.fontPath, cfg.fontSize);
    LogBus::attach(battleFeed.get());
    LogBus::setEchoToStdout(false);

    EventManager::getInstance().subscribe(EventType::RoundPhaseChanged,
        [](const Event& e){
            const auto& ev = static_cast<const RoundPhaseChangedEvent&>(e);
            LogBus::colored("Phase: " + ev.previousPhase + " → " + ev.nextPhase,
                            {0.75f, 0.9f, 1.0f}, 3.0f);
        });

    preloadCommonModels(*ctx);

    stateManager->pushState(std::make_unique<ScriptedState>(
        stateManager.get(), gameWorld.get(), "scripts/states/starter.lua"));

    if (ctx->setTitle) ctx->setTitle("Pokemon Autochess");
    std::cout << "[Init] GameApp initialized.\n";
}

void GameApp::handleEvent(const InputEvent& event) {
    // Temporary: convert back to SDL_Event to feed existing systems.
    SDL_Event SDL;
    if (!SDLInputAdapter::toSDLEvent(event, SDL)) return;

    if (cameraSystem) cameraSystem->handleZoom(SDL);
    if (stateManager) stateManager->handleInput(SDL);
}

void GameApp::fixedUpdate(float dt) {
    SystemRegistry::getInstance().updateAll(dt);

    if (stateManager) stateManager->update(dt);
    if (gameWorld)    gameWorld->update(dt);

    if (battleFeed)   battleFeed->update(dt);
}

void GameApp::render(int drawableW, int drawableH) {
    if (board && camera) board->draw(*camera);

    if (gameWorld && camera && board) {
        gameWorld->drawAll(*camera, *board);
    }

    if (stateManager) stateManager->render();

    if (gameWorld && camera) {
        auto healthBarData = gameWorld->getHealthBarData(*camera, drawableW, drawableH);
        healthBarRenderer.render(healthBarData);
    }

    if (shopSystem) shopSystem->renderUI(drawableW, drawableH);
    if (battleFeed) battleFeed->render(drawableW, drawableH);
}

void GameApp::shutdown() {
    std::cout << "[Shutdown] GameApp...\n";

    if (board) {
        board->shutdown();
        board.reset();
    }

    UIManager::shutdown();

    battleFeed.reset();
    shopSystem.reset();
    unitSystem.reset();
    cameraSystem.reset();

    stateManager.reset();
    gameWorld.reset();

    SystemRegistry::getInstance().clear();

    std::cout << "[Shutdown] GameApp done.\n";
}

void GameApp::preloadCommonModels(GameContext& c) {
    auto addByName = [&](std::vector<std::string>& out, const std::string& name) {
        const PokemonStats* stats = PokemonConfigLoader::getInstance().getStats(name);
        if (!stats) return;
        out.push_back("assets/models/" + stats->model);
    };

    std::vector<std::string> modelsToPreload;
    modelsToPreload.reserve(16);

    addByName(modelsToPreload, "bulbasaur");
    addByName(modelsToPreload, "charmander");
    addByName(modelsToPreload, "squirtle");
    addByName(modelsToPreload, "pidgey");
    addByName(modelsToPreload, "rattata");

    if (modelsToPreload.empty()) return;

    const int prevSwap = SDL_GL_GetSwapInterval();
    SDL_GL_SetSwapInterval(0);

    if (c.setTitle) c.setTitle("PokemonAutochess - Loading...");
    if (c.renderBootLoading) c.renderBootLoading(0.0f);

    if (c.pumpPreloadEvents && !c.pumpPreloadEvents()) std::exit(0);

    const int total = (int)modelsToPreload.size();
    for (int i = 0; i < total; ++i) {
        const std::string& path = modelsToPreload[i];

        if (c.setTitle) {
            c.setTitle(
                "PokemonAutochess - Loading " +
                std::to_string(i + 1) + "/" + std::to_string(total) + "  " + path
            );
        }

        if (c.pumpPreloadEvents && !c.pumpPreloadEvents()) std::exit(0);

        ResourceManager::getInstance().getModel(path);

        if (c.renderBootLoading) c.renderBootLoading(float(i + 1) / float(total));
    }

    if (c.setTitle) c.setTitle("Pokemon Autochess");
    if (c.pumpPreloadEvents) c.pumpPreloadEvents();

    SDL_GL_SetSwapInterval(prevSwap);
}
