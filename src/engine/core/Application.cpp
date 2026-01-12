// Application.cpp

#include "Application.h"
#include "Window.h"

#include "../events/Event.h"
#include "../events/EventManager.h"
#include "../events/RoundEvents.h"

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
#include "../../game/MovesConfigLoader.h"

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
#include <cmath>

namespace {
    constexpr unsigned int START_W  = 1280;
    constexpr unsigned int START_H = 720;

    static int scaledMouseX(int x, float s) { return (int)std::lround((float)x * s); }
    static int scaledMouseY(int y, float s) { return (int)std::lround((float)y * s); }
}

Application::Application() { init(); }
Application::~Application() { shutdown(); }

void Application::updateDrawableSizeAndViewport() {
    if (!window || !window->getSDLWindow()) return;

    SDL_GetWindowSize(window->getSDLWindow(), &windowW, &windowH);
    SDL_GL_GetDrawableSize(window->getSDLWindow(), &drawableW, &drawableH);

    if (drawableW <= 0) drawableW = windowW;
    if (drawableH <= 0) drawableH = windowH;

    glViewport(0, 0, drawableW, drawableH);
}

void Application::updateMouseScale() {
    // Mouse events come in window (logical) coords; UI projection uses drawable coords.
    // On HiDPI / Windows scaling, these differ -> clicks miss.
    if (windowW > 0 && windowH > 0) {
        mouseScaleX = (float)drawableW / (float)windowW;
        mouseScaleY = (float)drawableH / (float)windowH;
    } else {
        mouseScaleX = 1.0f;
        mouseScaleY = 1.0f;
    }
}

void Application::preloadCommonModels() {
    // Preload models that you know you will use early to avoid hitching mid-combat.
    // IMPORTANT: must be called AFTER GL context + glad are ready.
    auto preloadByName = [&](const std::string& name) {
        const PokemonStats* stats = PokemonConfigLoader::getInstance().getStats(name);
        if (!stats) return;
        const std::string path = "assets/models/" + stats->model;
        ResourceManager::getInstance().getModel(path);
    };

    // starters
    preloadByName("bulbasaur");
    preloadByName("charmander");
    preloadByName("squirtle");

    // route1 (from scripts/states/route1.lua)
    preloadByName("pidgey");
    preloadByName("rattata");
}

void Application::init() {
    if (TTF_Init() == -1) {
        std::cerr << "[Application] TTF_Init error: " << TTF_GetError() << "\n";
    }

    // Load configs
    PokemonConfigLoader::getInstance().loadConfig("config/pokemon_config.json");
    MovesConfigLoader::getInstance().loadConfig("config/moves_config.json");

    std::cout << "[Init] CWD: " << std::filesystem::current_path() << "\n";

    window = std::make_unique<Window>("Pokemon Autochess", (int)START_W, (int)START_H);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        std::exit(EXIT_FAILURE);
    }

    // NEW: Ensure viewport matches drawable size from the start.
    updateDrawableSizeAndViewport();
    updateMouseScale();

    glEnable(GL_DEPTH_TEST);

    renderer = std::make_unique<Renderer>();
    camera   = std::make_unique<Camera3D>(45.0f, float(drawableW) / float(drawableH), 0.1f, 100.0f);

    const auto& cfg = GameConfig::get();
    board = std::make_unique<BoardRenderer>(cfg.rows, cfg.cols, cfg.cellSize);

    gameWorld    = std::make_unique<GameWorld>();
    stateManager = std::make_unique<GameStateManager>();

    cameraSystem = std::make_shared<CameraSystem>(camera.get());
    unitSystem   = std::make_shared<UnitInteractionSystem>(camera.get(), gameWorld.get(), drawableW, drawableH);

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

    // NEW: Console logging can stall badly on Windows in Debug.
    // You can re-enable it if needed for debugging.
    LogBus::setEchoToStdout(false);

    EventManager::getInstance().subscribe(EventType::RoundPhaseChanged,
        [](const Event& e){
            const auto& ev = static_cast<const RoundPhaseChangedEvent&>(e);
            LogBus::colored("Phase: " + ev.previousPhase + " → " + ev.nextPhase,
                            {0.75f, 0.9f, 1.0f}, 3.0f);
        });

    // NEW: preload to remove mid-game hitches from synchronous loading
    preloadCommonModels();

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
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
               (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }

            // NEW: resize handling
            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    updateDrawableSizeAndViewport();
                    updateMouseScale();

                    // Keep camera aspect correct
                    if (camera) {
                        *camera = Camera3D(45.0f, float(drawableW) / float(drawableH), 0.1f, 100.0f);
                    }
                }
            }

            if (cameraSystem) cameraSystem->handleZoom(event);

            // NEW: scale mouse coords -> drawable coords before emitting
            switch (event.type) {
                case SDL_MOUSEBUTTONDOWN: {
                    int mx = scaledMouseX(event.button.x, mouseScaleX);
                    int my = scaledMouseY(event.button.y, mouseScaleY);
                    MouseButtonDownEvent mbe(mx, my);
                    EventManager::getInstance().emit(mbe);
                    break;
                }
                case SDL_MOUSEBUTTONUP: {
                    int mx = scaledMouseX(event.button.x, mouseScaleX);
                    int my = scaledMouseY(event.button.y, mouseScaleY);
                    MouseButtonUpEvent mue(mx, my);
                    EventManager::getInstance().emit(mue);
                    break;
                }
                case SDL_MOUSEMOTION: {
                    int mx = scaledMouseX(event.motion.x, mouseScaleX);
                    int my = scaledMouseY(event.motion.y, mouseScaleY);
                    MouseMotionEvent mme(mx, my);
                    EventManager::getInstance().emit(mme);
                    break;
                }
                default: break;
            }

            if (stateManager) stateManager->handleInput(event);
        }

        auto now = clock::now();
        double frameDt = std::chrono::duration<double>(now - previous).count();
        frameDt = std::min(frameDt, 0.25);
        previous = now;
        accumulator += frameDt;

        while (accumulator >= TIME_STEP) {
            SystemRegistry::getInstance().updateAll(TIME_STEP);
            if (stateManager) stateManager->update(TIME_STEP);

            if (gameWorld) gameWorld->update(TIME_STEP);

            update();
            accumulator -= TIME_STEP;
            if (battleFeed) battleFeed->update(TIME_STEP);
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (board && camera) {
            board->draw(*camera);
        }
        if (gameWorld && camera && board) {
            gameWorld->drawAll(*camera, *board);
        }
        if (stateManager) stateManager->render();

        // Use drawable size (NOT hardcoded 1280x720)
        if (gameWorld && camera) {
            auto healthBarData = gameWorld->getHealthBarData(*camera, drawableW, drawableH);
            healthBarRenderer.render(healthBarData);
        }
        if (shopSystem) shopSystem->renderUI(drawableW, drawableH);
        if (battleFeed) battleFeed->render(drawableW, drawableH);

        SDL_GL_SwapWindow(window->getSDLWindow());

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
