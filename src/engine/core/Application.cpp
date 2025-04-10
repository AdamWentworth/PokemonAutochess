// Application.cpp

#include "Application.h"
#include "Window.h"
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

// Removed global BoardRenderer* board declaration

Application::Application() {
    init();
}

Application::~Application() {
    shutdown();
}

void Application::init() {
    // Initialize SDL_ttf
    if (TTF_Init() == -1) {
        std::cerr << "[Application] TTF_Init error: " << TTF_GetError() << "\n";
        // Optionally handle the error (exit or disable text rendering)
    }
    
    // Load the Pokémon configuration.
    PokemonConfigLoader::getInstance().loadConfig("config/pokemon_config.json");
    std::cout << "[Init] Current working directory: " << std::filesystem::current_path() << "\n";

    // Create engine subsystems using smart pointers.
    window = std::make_unique<Window>("Pokemon Autochess", WIDTH, HEIGHT);
    
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        exit(EXIT_FAILURE);
    }

    glEnable(GL_DEPTH_TEST);

    healthBarRenderer.init();

    renderer = std::make_unique<Renderer>();
    camera = std::make_unique<Camera3D>(45.0f, static_cast<float>(WIDTH) / HEIGHT, 0.1f, 100.0f);
    
    // Allocate the BoardRenderer using a unique_ptr (improves lifetime management)
    board = std::make_unique<BoardRenderer>(8, 8, 1.2f);

    gameWorld = std::make_unique<GameWorld>();
    stateManager = std::make_unique<GameStateManager>();

    // Use smart pointers for the systems.
    cameraSystem = std::make_shared<CameraSystem>(camera.get());
    unitSystem = std::make_shared<UnitInteractionSystem>(camera.get(), gameWorld.get(), WIDTH, HEIGHT);

    SystemRegistry::getInstance().registerSystem(cameraSystem);
    SystemRegistry::getInstance().registerSystem(unitSystem);

    // Push the starter selection state.
    stateManager->pushState(std::make_unique<StarterSelectionState>(stateManager.get(), gameWorld.get()));

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

        SystemRegistry::getInstance().updateAll(deltaTime); // All systems update here
        if (stateManager) stateManager->update(deltaTime);
        update();

        // Rendering
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        board->draw(*camera);
        gameWorld->drawAll(*camera, *board);

        if (stateManager) stateManager->render();

        // Render health bars AFTER game objects but BEFORE buffer swap
        auto healthBarData = gameWorld->getHealthBarData(*camera, WIDTH, HEIGHT);
        healthBarRenderer.render(healthBarData);

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
    
    if (renderer) {
        renderer->shutdown();
        renderer.reset();
    }

    // Shutdown the board renderer and let unique_ptr automatically free memory.
    if (board) {
        board->shutdown();
        board.reset();
    }

    // Smart pointers are automatically cleaned up.
    stateManager.reset();
    gameWorld.reset();
    camera.reset();
    window.reset();

    SystemRegistry::getInstance().clear(); // Optional cleanup

    // Shutdown SDL_ttf
    TTF_Quit();

    UIManager::shutdown(); 
    
    std::cout << "[Shutdown] Shutdown complete.\n";
}