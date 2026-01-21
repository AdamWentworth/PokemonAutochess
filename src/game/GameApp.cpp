// src/game/GameApp.cpp

#include "game/GameApp.h"

#include "engine/core/GameContext.h"
#include "engine/core/SystemRegistry.h"
#include "engine/input/InputEvent.h"

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

#include <SDL2/SDL.h> // Only used internally as an adapter. Keep it out of GameLoop boundary.

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstring> // memset

GameApp::GameApp() = default;
GameApp::~GameApp() = default;

void GameApp::init(GameContext& ctx) {
    // Load configs (game-specific)
    PokemonConfigLoader::getInstance().loadConfig("config/pokemon_config.json");
    MovesConfigLoader::getInstance().loadConfig("config/moves_config.json");

    std::cout << "[Init] CWD: " << std::filesystem::current_path() << "\n";

    camera = ctx.camera;

    const auto& cfg = GameConfig::get();
    board = std::make_unique<BoardRenderer>(cfg.rows, cfg.cols, cfg.cellSize);

    gameWorld    = std::make_unique<GameWorld>();
    stateManager = std::make_unique<GameStateManager>();

    // Systems (game-specific)
    cameraSystem = std::make_shared<CameraSystem>(camera);
    unitSystem   = std::make_shared<UnitInteractionSystem>(
        camera, gameWorld.get(), ctx.drawableW, ctx.drawableH
    );

    SystemRegistry::getInstance().registerSystem(cameraSystem);
    SystemRegistry::getInstance().registerSystem(unitSystem);

    auto roundSystem = std::make_shared<RoundSystem>();
    SystemRegistry::getInstance().registerSystem(roundSystem);

    shopSystem = std::make_shared<ShopSystem>();
    SystemRegistry::getInstance().registerSystem(shopSystem);

    healthBarRenderer.init();

    // Battle feed + LogBus
    battleFeed = std::make_unique<BattleFeed>(cfg.fontPath, cfg.fontSize);
    LogBus::attach(battleFeed.get());

    // Console logging can stall badly on Windows in Debug.
    LogBus::setEchoToStdout(false);

    EventManager::getInstance().subscribe(EventType::RoundPhaseChanged,
        [](const Event& e){
            const auto& ev = static_cast<const RoundPhaseChangedEvent&>(e);
            LogBus::colored("Phase: " + ev.previousPhase + " → " + ev.nextPhase,
                            {0.75f, 0.9f, 1.0f}, 3.0f);
        });

    // Preload (game-level decision, uses engine loading UI via ctx)
    preloadCommonModels(ctx);

    stateManager->pushState(std::make_unique<ScriptedState>(
        stateManager.get(), gameWorld.get(), "scripts/states/starter.lua"));

    if (ctx.setTitle) ctx.setTitle("Pokemon Autochess");

    std::cout << "[Init] Game initialized.\n";
}

void GameApp::handleEvent(const InputEvent& event) {
    // Adapter: some existing code expects SDL_Event&
    SDL_Event sdl;
    std::memset(&sdl, 0, sizeof(sdl));

    switch (event.type) {
        case InputEvent::Type::MouseWheel:
            sdl.type = SDL_MOUSEWHEEL;
            sdl.wheel.x = event.wheelX;
            sdl.wheel.y = event.wheelY;
            break;

        case InputEvent::Type::KeyDown:
            sdl.type = SDL_KEYDOWN;
            sdl.key.keysym.sym = (SDL_Keycode)event.key;
            sdl.key.repeat = event.repeat ? 1 : 0;
            break;

        case InputEvent::Type::KeyUp:
            sdl.type = SDL_KEYUP;
            sdl.key.keysym.sym = (SDL_Keycode)event.key;
            break;

        case InputEvent::Type::MouseMove:
            sdl.type = SDL_MOUSEMOTION;
            sdl.motion.x = event.mouseX;
            sdl.motion.y = event.mouseY;
            break;

        case InputEvent::Type::MouseDown:
            sdl.type = SDL_MOUSEBUTTONDOWN;
            sdl.button.x = event.mouseX;
            sdl.button.y = event.mouseY;
            sdl.button.button = (Uint8)event.mouseButton;
            break;

        case InputEvent::Type::MouseUp:
            sdl.type = SDL_MOUSEBUTTONUP;
            sdl.button.x = event.mouseX;
            sdl.button.y = event.mouseY;
            sdl.button.button = (Uint8)event.mouseButton;
            break;

        default:
            // Quit/Resize/Unknown: ignore here for now
            return;
    }

    // Camera zoom is a game-level feature
    if (cameraSystem) cameraSystem->handleZoom(sdl);

    // Let states handle raw input
    if (stateManager) stateManager->handleInput(sdl);
}

void GameApp::fixedUpdate(float dt) {
    SystemRegistry::getInstance().updateAll(dt);

    if (stateManager) stateManager->update(dt);
    if (gameWorld)    gameWorld->update(dt);

    if (battleFeed)   battleFeed->update(dt);
}

void GameApp::render(int drawableW, int drawableH) {
    if (board && camera) {
        board->draw(*camera);
    }

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
    std::cout << "[Shutdown] Game...\n";

    // Game-owned GL resources
    if (board) {
        board->shutdown();
        board.reset();
    }

    // Engine global UI manager
    UIManager::shutdown();

    battleFeed.reset();
    shopSystem.reset();
    unitSystem.reset();
    cameraSystem.reset();

    stateManager.reset();
    gameWorld.reset();

    SystemRegistry::getInstance().clear();

    std::cout << "[Shutdown] Game done.\n";
}

void GameApp::preloadCommonModels(GameContext& ctx) {
    // Preload models that you know you will use early to avoid hitching mid-combat.
    // IMPORTANT: must be called AFTER GL context + glad are ready.

    auto addByName = [&](std::vector<std::string>& out, const std::string& name) {
        const PokemonStats* stats = PokemonConfigLoader::getInstance().getStats(name);
        if (!stats) return;
        const std::string path = "assets/models/" + stats->model;
        out.push_back(path);
    };

    std::vector<std::string> modelsToPreload;
    modelsToPreload.reserve(16);

    // starters
    addByName(modelsToPreload, "bulbasaur");
    addByName(modelsToPreload, "charmander");
    addByName(modelsToPreload, "squirtle");

    // route1 (from scripts/states/route1.lua)
    addByName(modelsToPreload, "pidgey");
    addByName(modelsToPreload, "rattata");

    if (modelsToPreload.empty()) return;

    if (ctx.setTitle) ctx.setTitle("PokemonAutochess - Loading...");

    // draw initial bar
    if (ctx.renderBootLoading) ctx.renderBootLoading(0.0f);

    if (ctx.pumpPreloadEvents && !ctx.pumpPreloadEvents()) std::exit(0);

    const int total = (int)modelsToPreload.size();
    for (int i = 0; i < total; ++i) {
        const std::string& path = modelsToPreload[i];

        if (ctx.setTitle) {
            ctx.setTitle(
                "PokemonAutochess - Loading " +
                std::to_string(i + 1) + "/" + std::to_string(total) + "  " + path
            );
        }

        if (ctx.pumpPreloadEvents && !ctx.pumpPreloadEvents()) std::exit(0);

        // expensive load
        ResourceManager::getInstance().getModel(path);

        float progress = float(i + 1) / float(total);
        if (ctx.renderBootLoading) ctx.renderBootLoading(progress);
    }

    if (ctx.setTitle) ctx.setTitle("Pokemon Autochess");
    if (ctx.pumpPreloadEvents) ctx.pumpPreloadEvents();
}
