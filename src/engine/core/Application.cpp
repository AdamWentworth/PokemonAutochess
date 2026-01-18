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

// NEW: loading bar
#include "../ui/BootLoadingView.h"

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
#include <vector>

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

// NEW: keep OS happy while loading (Option A)
bool Application::pumpPreloadEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) return false;

        if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                e.window.event == SDL_WINDOWEVENT_RESIZED) {
                updateDrawableSizeAndViewport();
                updateMouseScale();

                if (camera && drawableW > 0 && drawableH > 0) {
                    *camera = Camera3D(45.0f, float(drawableW) / float(drawableH), 0.1f, 100.0f);
                }
            }
        }
    }
    return true;
}

void Application::preloadCommonModels() {
    // Preload models that you know you will use early to avoid hitching mid-combat.
    // IMPORTANT: must be called AFTER GL context + glad are ready.

    if (!window) return;

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

    // --- NEW: Disable vsync during preload so swapBuffers() doesn't block ---
    const int prevSwap = SDL_GL_GetSwapInterval();   // -1,0,1 typically
    SDL_GL_SetSwapInterval(0);

    // Option A: show immediately that we're alive
    window->setTitle("PokemonAutochess - Loading...");
    updateDrawableSizeAndViewport();

    // Option B: draw a real progress bar (no fonts)
    bootLoadingView.render(0.0f, drawableW, drawableH);
    window->swapBuffers();

    if (!pumpPreloadEvents()) std::exit(0);

    const int total = (int)modelsToPreload.size();
    for (int i = 0; i < total; ++i) {
        updateDrawableSizeAndViewport();

        const std::string& path = modelsToPreload[i];

        // Option A: title progress
        window->setTitle(
            "PokemonAutochess - Loading " +
            std::to_string(i + 1) + "/" + std::to_string(total) + "  " + path
        );

        // Keep window responsive
        if (!pumpPreloadEvents()) std::exit(0);

        // Expensive load
        ResourceManager::getInstance().getModel(path);

        // Update bar and present a frame
        float progress = float(i + 1) / float(total);
        bootLoadingView.render(progress, drawableW, drawableH);
        window->swapBuffers();
    }

    window->setTitle("Pokemon Autochess");
    pumpPreloadEvents();

    // --- NEW: Restore prior vsync mode for normal gameplay ---
    SDL_GL_SetSwapInterval(prevSwap);
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

    // Ensure viewport matches drawable size from the start.
    updateDrawableSizeAndViewport();
    updateMouseScale();

    glEnable(GL_DEPTH_TEST);

    // NEW: create boot loading view now that GL is ready
    bootLoadingView.init();

    // Option A: set title early + show a frame
    window->setTitle("PokemonAutochess - Loading...");
    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    window->swapBuffers();
    pumpPreloadEvents();

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

    // Console logging can stall badly on Windows in Debug.
    LogBus::setEchoToStdout(false);

    EventManager::getInstance().subscribe(EventType::RoundPhaseChanged,
        [](const Event& e){
            const auto& ev = static_cast<const RoundPhaseChangedEvent&>(e);
            LogBus::colored("Phase: " + ev.previousPhase + " → " + ev.nextPhase,
                            {0.75f, 0.9f, 1.0f}, 3.0f);
        });

    // UPDATED: preload uses Option A + Option B
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

            // resize handling
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

            // scale mouse coords -> drawable coords before emitting
            switch (event.type) {
                case SDL_MOUSEBUTTONDOWN: {
                    int mx = scaledMouseX(event.button.x, mouseScaleX);
                    int my = scaledMouseY(event.button.y, mouseScaleY);
                    MouseButtonDownEvent mbe(mx, my, event.button.button);
                    EventManager::getInstance().emit(mbe);
                    break;
                }
                case SDL_MOUSEBUTTONUP: {
                    int mx = scaledMouseX(event.button.x, mouseScaleX);
                    int my = scaledMouseY(event.button.y, mouseScaleY);
                    MouseButtonUpEvent mue(mx, my, event.button.button);
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
