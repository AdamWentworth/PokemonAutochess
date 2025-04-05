// Application.cpp

#include "Application.h"
#include "../utils/ResourceManager.h"
#include "../render/Renderer.h"
#include "../render/BoardRenderer.h"
#include "../render/Model.h"
#include "../../game/GameWorld.h"
#include "../../game/GameStateManager.h" 
#include "../../game/state/StarterSelectionState.h"
#include "Window.h" // Include our new Window header

#define NOMINMAX
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>       // Must come before GL
#endif

#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp> // For glm::unProject

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
    std::cout << "[Init] Current working directory: " << std::filesystem::current_path() << "\n";
    
    // Create the window using our new Window class
    window = new Window("Pokemon Autochess", WIDTH, HEIGHT);
    
    // Load OpenGL functions using glad
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        exit(EXIT_FAILURE);
    }
    
    glEnable(GL_DEPTH_TEST);
    
    std::cout << "[Init] GLAD initialized. OpenGL version: " << glGetString(GL_VERSION) << "\n";
    
    // Initialize renderer, camera, board, and game world as before
    renderer = new Renderer();
    camera = new Camera3D(45.0f, static_cast<float>(WIDTH) / HEIGHT, 0.1f, 100.0f);
    board = new BoardRenderer(8, 8, 1.0f);
    gameWorld = new GameWorld();
    stateManager = new GameStateManager();
    stateManager->pushState(std::make_unique<StarterSelectionState>(stateManager, gameWorld));
    
    std::cout << "[Init] Application initialized.\n";
}

void Application::run() {
    std::cout << "[Run] Entering main loop...\n";
    auto lastTime = std::chrono::high_resolution_clock::now();
    int frameCount = 0;
    bool running = true;

    bool carryingModel = false;
    int carryingPokemonIndex = -1;
    bool panningCamera = false;
    int lastMouseX = 0;
    int lastMouseY = 0;
    float pickRadius = 0.7f;
    SDL_Event event;

    auto screenToWorld = [&](int mouseX, int mouseY) -> glm::vec3 {
        glm::vec4 viewport(0.0f, 0.0f, WIDTH, HEIGHT);
        float winX = static_cast<float>(mouseX);
        float winY = static_cast<float>(HEIGHT - mouseY);
        glm::vec3 nearPoint = glm::unProject(glm::vec3(winX, winY, 0.0f),
                                             camera->getViewMatrix(),
                                             camera->getProjectionMatrix(),
                                             viewport);
        glm::vec3 farPoint  = glm::unProject(glm::vec3(winX, winY, 1.0f),
                                             camera->getViewMatrix(),
                                             camera->getProjectionMatrix(),
                                             viewport);
        glm::vec3 dir = glm::normalize(farPoint - nearPoint);
        float t = -nearPoint.y / dir.y;
        return nearPoint + t * dir;
    };

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                std::cout << "[Event] Quit event received.\n";
                running = false;
            }

            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                std::cout << "[Event] Escape key pressed. Exiting.\n";
                running = false;
            }

            if (event.type == SDL_MOUSEWHEEL) {
                camera->zoom(event.wheel.y * 0.5f);
            }

            if (stateManager) {
                stateManager->handleInput(event);
            }

            if (!dynamic_cast<StarterSelectionState*>(stateManager->getCurrentState())) {
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                    glm::vec3 clickPos3D = screenToWorld(event.button.x, event.button.y);
                    if (carryingPokemonIndex < 0) {
                        float closestDist = std::numeric_limits<float>::max();
                        int closestIndex = -1;
                        auto& allPokemons = gameWorld->getPokemons();
                        for (int i = 0; i < (int)allPokemons.size(); i++) {
                            float dist = glm::distance(clickPos3D, allPokemons[i].position);
                            if (dist < closestDist) {
                                closestDist = dist;
                                closestIndex = i;
                            }
                        }
                        if (closestIndex >= 0 && closestDist <= pickRadius) {
                            carryingPokemonIndex = closestIndex;
                            std::cout << "[Event] Picked up pokemon index " << closestIndex << "\n";
                        } else {
                            panningCamera = true;
                            lastMouseX = event.button.x;
                            lastMouseY = event.button.y;
                        }
                    } else {
                        std::cout << "[Event] Placed pokemon index " << carryingPokemonIndex << "\n";
                        carryingPokemonIndex = -1;
                    }
                }
                else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                    if (panningCamera) {
                        panningCamera = false;
                        std::cout << "[Event] Camera pan ended.\n";
                    }
                }
                else if (event.type == SDL_MOUSEMOTION) {
                    if (carryingPokemonIndex >= 0) {
                        const float minX = -3.5f, maxX = 3.5f, minZ = 0.5f, maxZ = 3.5f;
                        glm::vec3 newPos = screenToWorld(event.motion.x, event.motion.y);
                        newPos.x = std::floor(newPos.x) + 0.5f;
                        newPos.z = std::floor(newPos.z) + 0.5f;
                        newPos.y = 0.0f;
                        newPos.x = glm::clamp(newPos.x, minX, maxX);
                        newPos.z = glm::clamp(newPos.z, minZ, maxZ);
                        gameWorld->getPokemons()[carryingPokemonIndex].position = newPos;
                    }
                    else if (panningCamera) {
                        int dx = event.motion.x - lastMouseX;
                        int dy = event.motion.y - lastMouseY;
                        lastMouseX = event.motion.x;
                        lastMouseY = event.motion.y;
                        float panSpeed = 0.02f;
                        camera->move(glm::vec3(-dx * panSpeed, 0.0f, -dy * panSpeed));
                    }
                }
            }
        }

        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        if (stateManager) {
            stateManager->update(deltaTime);
        }
        update();

        const Uint8* keystates = SDL_GetKeyboardState(nullptr);
        float cameraSpeed = 0.1f;
        glm::vec3 moveDir(0.0f);
        if (keystates[SDL_SCANCODE_W] || keystates[SDL_SCANCODE_UP])    moveDir.z -= cameraSpeed;
        if (keystates[SDL_SCANCODE_S] || keystates[SDL_SCANCODE_DOWN])  moveDir.z += cameraSpeed;
        if (keystates[SDL_SCANCODE_A] || keystates[SDL_SCANCODE_LEFT])  moveDir.x -= cameraSpeed;
        if (keystates[SDL_SCANCODE_D] || keystates[SDL_SCANCODE_RIGHT]) moveDir.x += cameraSpeed;
        if (glm::length(moveDir) > 0.0f) {
            camera->move(moveDir);
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        board->draw(*camera);
        gameWorld->drawAll(*camera);
        if (stateManager) {
            stateManager->render();
        }
        
        // Swap buffers using our Window object
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
        delete renderer;
    }
    if (camera) delete camera;
    if (board) {
        board->shutdown();
        delete board;
    }
    if (gameWorld) delete gameWorld;
    if (stateManager) delete stateManager;
    if (window) {
        delete window;
    }
    std::cout << "[Shutdown] Shutdown complete.\n";
}
