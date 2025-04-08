// Application.cpp

#include "Application.h"
#include "Window.h"
#include "../render/Renderer.h"
#include "../render/BoardRenderer.h"
#include "../render/Model.h"
#include "../utils/ResourceManager.h"
#include "../../game/GameWorld.h"
#include "../../game/GameStateManager.h"
#include "../../game/PokemonConfigLoader.h"
#include "../../game/systems/CameraSystem.h"
#include "../../game/systems/UnitInteractionSystem.h"
#include "../../game/systems/RoundSystem.h"
#include "../../game/state/StarterSelectionState.h"

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

const unsigned int WIDTH = 1280;
const unsigned int HEIGHT = 720;

BoardRenderer* board = nullptr;

Application::Application() {
    init();
}

Application::~Application() {
    shutdown();
}

void Application::init() {
    PokemonConfigLoader::getInstance().loadConfig("config/pokemon_config.json");

    std::cout << "[Init] Current working directory: " << std::filesystem::current_path() << "\n";

    window = new Window("Pokemon Autochess", WIDTH, HEIGHT);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        exit(EXIT_FAILURE);
    }

    glEnable(GL_DEPTH_TEST);

    renderer = new Renderer();
    camera = new Camera3D(45.0f, static_cast<float>(WIDTH) / HEIGHT, 0.1f, 100.0f);
    board = new BoardRenderer(8, 8, 1.0f);
    gameWorld = new GameWorld();
    stateManager = new GameStateManager();

    cameraSystem = std::make_shared<CameraSystem>(camera);
    unitSystem = std::make_shared<UnitInteractionSystem>(camera, gameWorld, WIDTH, HEIGHT);

    SystemRegistry::getInstance().registerSystem(cameraSystem);
    SystemRegistry::getInstance().registerSystem(unitSystem);

    stateManager->pushState(std::make_unique<StarterSelectionState>(stateManager, gameWorld));

    auto roundSystem = std::make_shared<RoundSystem>();
    SystemRegistry::getInstance().registerSystem(roundSystem);

    std::cout << "[Init] Application initialized.\n";
}

void Application::run() {
    std::cout << "[Run] Entering main loop...\n";
    auto lastTime = std::chrono::high_resolution_clock::now();
    int frameCount = 0;
    bool running = true;

    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }

            cameraSystem->handleZoom(event);
            cameraSystem->handleMousePan(event);

            if (stateManager) stateManager->handleInput(event);
            if (!dynamic_cast<StarterSelectionState*>(stateManager->getCurrentState())) {
                unitSystem->handleEvent(event);
            }
        }

        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        SystemRegistry::getInstance().updateAll(deltaTime); // ✅ All systems update here
        if (stateManager) stateManager->update(deltaTime);
        update();

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        board->draw(*camera);
        gameWorld->drawAll(*camera);

        if (stateManager) stateManager->render();

        SDL_GL_SwapWindow(window->getSDLWindow());

        frameCount++;
        if (deltaTime >= 1.0f) {
            std::cout << "[FPS] " << frameCount << " frames/sec\n";
            frameCount = 0;
        }
    }
}

void Application::update() {
    // Global updates if needed
}

void Application::shutdown() {
    std::cout << "[Shutdown] Shutting down...\n";
    if (renderer) { renderer->shutdown(); delete renderer; }
    if (camera) delete camera;
    if (board) { board->shutdown(); delete board; }
    if (gameWorld) delete gameWorld;
    if (stateManager) delete stateManager;
    if (window) delete window;

    SystemRegistry::getInstance().clear(); // ✅ Optional cleanup of shared_ptr registry
    std::cout << "[Shutdown] Shutdown complete.\n";
}

