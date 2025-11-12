// Application.cpp

#include "Application.h"
#include "Window.h"

#include "../events/Event.h"
#include "../events/EventManager.h"

#include "../render/Renderer.h"
#include "../render/BoardRenderer.h"
#include "../render/Model.h"

#include "../utils/ResourceManager.h"

#include "../ui/HealthBarRenderer.h"
#include "../ui/UIManager.h"
#include "../ui/BattleFeed.h"

#include "../../game/GameWorld.h"
#include "../../game/GameStateManager.h"
#include "../../game/PokemonConfigLoader.h"
#include "../../game/systems/CameraSystem.h"
#include "../../game/systems/UnitInteractionSystem.h"
#include "../../game/systems/RoundSystem.h"
#include "../../game/systems/ShopSystem.h"
#include "../../game/ScriptedState.h"
#include "../../game/GameConfig.h"
#include "../../game/LogBus.h"

#define NOMINMAX
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <glad/glad.h>

#include <iostream>
#include <chrono>
#include <filesystem>
#include <algorithm>

namespace {
    constexpr unsigned int WIDTH  = 1280;
    constexpr unsigned int HEIGHT = 720;
}

Application::Application() { init(); }
Application::~Application() { shutdown(); }

void Application::init() {
    if (TTF_Init() == -1) {
        std::cerr << "[Application] TTF_Init error: " << TTF_GetError() << "\n";
    }

    PokemonConfigLoader::getInstance().loadConfig("config/pokemon_config.json");

    std::cout << "[Init] CWD: " << std::filesystem::current_path() << "\n";

    window = std::make_unique<Window>("Pokemon Autochess", WIDTH, HEIGHT);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        std::exit(EXIT_FAILURE);
    }

    glEnable(GL_DEPTH_TEST);

    renderer = std::make_unique<Renderer>();
    camera   = std::make_unique<Camera3D>(45.0f, float(WIDTH) / float(HEIGHT), 0.1f, 100.0f);

    const auto& cfg = GameConfig::get();
    board = std::make_unique<BoardRenderer>(cfg.rows, cfg.cols, cfg.cellSize);

    gameWorld    = std::make_unique<GameWorld>();
    stateManager = std::make_unique<GameStateManager>();

    cameraSystem = std::make_shared<CameraSystem>(camera.get());
    unitSystem   = std::make_shared<UnitInteractionSystem>(camera.get(), gameWorld.get(), WIDTH, HEIGHT);

    SystemRegistry::getInstance().registerSystem(cameraSystem);
    SystemRegistry::getInstance().registerSystem(unitSystem);

    auto roundSystem = std::make_shared<RoundSystem>();
    SystemRegistry::getInstance().registerSystem(roundSystem);

    shopSystem = std::make_shared<ShopSystem>();
    SystemRegistry::getInstance().registerSystem(shopSystem);

    healthBarRenderer.init();

    battleFeed = std::make_unique<BattleFeed>(cfg.fontPath, cfg.fontSize);
    LogBus::attach(battleFeed.get());

    stateManager->pushState(std::make_unique<ScriptedState>(
        stateManager.get(), gameWorld.get(), "scripts/states/starter.lua"));

    std::cout << "[Init] Application initialized.\n";
}

void Application::run() {
    std::cout << "[Run] Main loop @ 60 Hz...\n";

    using clock = std::chrono::high_resolution_clock;
    auto previous = clock::now();
    double accumulator = 0.0;
    int frameCount = 0;
    bool running = true;
    SDL_Event event;

    while (running) {
        // --- Input ---
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
               (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }

            if (cameraSystem) cameraSystem->handleZoom(event);

            switch (event.type) {
                case SDL_MOUSEBUTTONDOWN: {
                    MouseButtonDownEvent mbe(event.button.x, event.button.y);
                    EventManager::getInstance().emit(mbe);
                    break;
                }
                case SDL_MOUSEBUTTONUP: {
                    MouseButtonUpEvent mue(event.button.x, event.button.y);
                    EventManager::getInstance().emit(mue);
                    break;
                }
                case SDL_MOUSEMOTION: {
                    MouseMotionEvent mme(event.motion.x, event.motion.y);
                    EventManager::getInstance().emit(mme);
                    break;
                }
                default: break;
            }

            if (stateManager) stateManager->handleInput(event);
        }

        // --- Fixed update ---
        auto now = clock::now();
        double frameDt = std::chrono::duration<double>(now - previous).count();
        frameDt = std::min(frameDt, 0.25);
        previous = now;
        accumulator += frameDt;

        while (accumulator >= TIME_STEP) {
            SystemRegistry::getInstance().updateAll(TIME_STEP);
            if (stateManager) stateManager->update(TIME_STEP);
            update();
            accumulator -= TIME_STEP;
            if (battleFeed) battleFeed->update(TIME_STEP);
        }

        // --- Render ---
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (board && camera) {
            board->draw(*camera);
        }

        if (gameWorld && camera && board) {
            gameWorld->drawAll(*camera, *board);
        }

        if (stateManager) stateManager->render();

        if (gameWorld && camera) {
            auto healthBarData = gameWorld->getHealthBarData(*camera, WIDTH, HEIGHT);
            healthBarRenderer.render(healthBarData);
        }

        if (shopSystem) shopSystem->renderUI(WIDTH, HEIGHT);

        // >>> Draw battle feed after all 3D/UI so it overlays everything
        if (battleFeed) {
            battleFeed->render(WIDTH, HEIGHT);
        }

        SDL_GL_SwapWindow(window->getSDLWindow());

        // --- FPS ---
        frameCount++;
        static double fpsTimer = 0.0;
        fpsTimer += frameDt;
        if (fpsTimer >= 1.0) {
            std::cout << "[FPS] " << frameCount << "\n";
            frameCount = 0;
            fpsTimer = 0.0;
        }
    }
}

void Application::update() {}

void Application::shutdown() {
    std::cout << "[Shutdown] ...\n";

    if (renderer) { renderer->shutdown(); renderer.reset(); }
    if (board)    { board->shutdown();    board.reset();   }

    UIManager::shutdown();

    stateManager.reset();
    gameWorld.reset();
    camera.reset();
    window.reset();

    SystemRegistry::getInstance().clear();

    TTF_Quit();

    std::cout << "[Shutdown] Done.\n";
}