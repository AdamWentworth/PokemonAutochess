// Application.cpp

#include "Application.h"
#include "Window.h"
#include "../events/Event.h"
#include "../events/EventManager.h"
#include "../render/Renderer.h"
#include "../render/BoardRenderer.h"
#include "../render/Model.h"
#include "../utils/ResourceManager.h"
#include "../ui/UIManager.h"
#include "../../game/GameWorld.h"
#include "../../game/GameStateManager.h"
#include "../../game/PokemonConfigLoader.h"
#include "../../game/systems/CameraSystem.h"
#include "../../game/systems/UnitInteractionSystem.h"
#include "../../game/systems/RoundSystem.h"
#include "../../game/state/StarterSelectionState.h"
#include "../../game/GameConfig.h"   // NEW

#define NOMINMAX
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <iostream>
#include <chrono>
#include <filesystem>
#include <SDL2/SDL_ttf.h>

const unsigned int WIDTH = 1280;
const unsigned int HEIGHT = 720;

Application::Application() {
    init();
}

Application::~Application() {
    shutdown();
}

void Application::init() {
    if (TTF_Init() == -1) {
        std::cerr << "[Application] TTF_Init error: " << TTF_GetError() << "\n";
    }
    
    PokemonConfigLoader::getInstance().loadConfig("config/pokemon_config.json");
    std::cout << "[Init] Current working directory: " << std::filesystem::current_path() << "\n";

    window = std::make_unique<Window>("Pokemon Autochess", WIDTH, HEIGHT);
    
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        exit(EXIT_FAILURE);
    }

    glEnable(GL_DEPTH_TEST);

    healthBarRenderer.init();

    renderer = std::make_unique<Renderer>();
    camera = std::make_unique<Camera3D>(45.0f, static_cast<float>(WIDTH) / HEIGHT, 0.1f, 100.0f);
    
    // Use Lua-driven board settings
    const auto& cfg = GameConfig::get();
    board = std::make_unique<BoardRenderer>(cfg.rows, cfg.cols, cfg.cellSize);

    gameWorld = std::make_unique<GameWorld>();
    stateManager = std::make_unique<GameStateManager>();

    cameraSystem = std::make_shared<CameraSystem>(camera.get());
    unitSystem = std::make_shared<UnitInteractionSystem>(camera.get(), gameWorld.get(), WIDTH, HEIGHT);

    SystemRegistry::getInstance().registerSystem(cameraSystem);
    SystemRegistry::getInstance().registerSystem(unitSystem);

    stateManager->pushState(std::make_unique<StarterSelectionState>(stateManager.get(), gameWorld.get()));

    auto roundSystem = std::make_shared<RoundSystem>();
    SystemRegistry::getInstance().registerSystem(roundSystem);

    std::cout << "[Init] Application initialized.\n";
}

void Application::run() {
    std::cout << "[Run] Entering main loop (fixed 60 Hz)...\n";

    using clock = std::chrono::high_resolution_clock;
    auto previous   = clock::now();
    double accumulator = 0.0;
    int frameCount = 0;
    bool running   = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || 
               (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
            cameraSystem->handleZoom(event);

            switch (event.type) {
                case SDL_MOUSEBUTTONDOWN: {
                    MouseButtonDownEvent mbe(event.button.x, event.button.y);
                    std::cout << "[Application] Emitting MouseButtonDownEvent at (" 
                              << event.button.x << ", " << event.button.y << ")\n";
                    EventManager::getInstance().emit(mbe);
                    break;
                }
                case SDL_MOUSEBUTTONUP: {
                    ::MouseButtonUpEvent mue(event.button.x, event.button.y);
                    EventManager::getInstance().emit(mue);
                    break;
                }
                case SDL_MOUSEMOTION: {
                    ::MouseMotionEvent mme(event.motion.x, event.motion.y);
                    EventManager::getInstance().emit(mme);
                    break;
                }
                default:
                    break;
            }
        
            if (stateManager) stateManager->handleInput(event);
            if (!dynamic_cast<StarterSelectionState*>(stateManager->getCurrentState())) {
                unitSystem->handleEvent(event);
            }
        }

        auto now       = clock::now();
        double frameDt = std::chrono::duration<double>(now - previous).count();
        frameDt = std::min(frameDt, 0.25);
        previous = now;
        accumulator += frameDt;

        while (accumulator >= TIME_STEP) {
            SystemRegistry::getInstance().updateAll(TIME_STEP);
            if (stateManager) stateManager->update(TIME_STEP);
            update();
            accumulator -= TIME_STEP;
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        board->draw(*camera);
        gameWorld->drawAll(*camera, *board);

        if (stateManager) stateManager->render();

        auto healthBarData = gameWorld->getHealthBarData(*camera, WIDTH, HEIGHT);
        healthBarRenderer.render(healthBarData);

        SDL_GL_SwapWindow(window->getSDLWindow());

        frameCount++;
        static double fpsTimer = 0.0;
        fpsTimer += frameDt;
        if (fpsTimer >= 1.0) {
            std::cout << "[FPS] " << frameCount << " frames/sec\n";
            frameCount = 0;
            fpsTimer   = 0.0;
        }
    }
}

void Application::update() {}

void Application::shutdown() {
    std::cout << "[Shutdown] Shutting down...\n";
    
    if (renderer) {
        renderer->shutdown();
        renderer.reset();
    }

    if (board) {
        board->shutdown();
        board.reset();
    }

    stateManager.reset();
    gameWorld.reset();
    camera.reset();
    window.reset();

    SystemRegistry::getInstance().clear();

    TTF_Quit();

    UIManager::shutdown(); 
    
    std::cout << "[Shutdown] Shutdown complete.\n";
}
