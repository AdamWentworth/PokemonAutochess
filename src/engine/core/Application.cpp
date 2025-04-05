// Application.cpp

#include "Application.h"
#include "ResourceManager.h"
#include "../render/Renderer.h"
#include "../render/BoardRenderer.h"
#include "../render/Model.h"
#include "../../game/GameWorld.h"
#include "../../game/GameStateManager.h" 
#include "../../game/StarterSelectionState.h"
#define NOMINMAX
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>       // Must come before GL
#endif

#include <SDL2/SDL.h>
#include <glad/glad.h>

#include <iostream>
#include <chrono>
#include <thread>  // For sleep (if needed)
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp> // For glm::unProject

SDL_Window* window = nullptr;
SDL_GLContext glContext = nullptr;
BoardRenderer* board = nullptr;

const unsigned int WIDTH = 1280;
const unsigned int HEIGHT = 720;

Application::Application() {
    init();
}

Application::~Application() {
    shutdown();
}

void Application::init() {
    std::cout << "[Init] Current working directory: " << std::filesystem::current_path() << "\n";
    std::cout << "[Init] Starting SDL initialization...\n";
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        std::cerr << "Failed to initialize SDL2: " << SDL_GetError() << "\n";
        exit(EXIT_FAILURE);
    }
    
    // Set OpenGL attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    window = SDL_CreateWindow(
        "Pokemon Autochess",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIDTH, HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << "\n";
        SDL_Quit();
        exit(EXIT_FAILURE);
    }
    std::cout << "[Init] SDL window created successfully.\n";

    std::cout << "[Init] Creating OpenGL context...\n";
    glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "Failed to create OpenGL context: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        exit(EXIT_FAILURE);
    }
    
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        exit(EXIT_FAILURE);
    }

    glEnable(GL_DEPTH_TEST);
    
    std::cout << "[Init] GLAD initialized. OpenGL version: " << glGetString(GL_VERSION) << "\n";    
    SDL_GL_SetSwapInterval(1); // Enable V-Sync

    std::cout << "[Init] Initialized SDL Window (" << WIDTH << "x" << HEIGHT << ")\n";

    renderer = new Renderer();

    camera = new Camera3D(45.0f, static_cast<float>(WIDTH) / HEIGHT, 0.1f, 100.0f);

    // Create the board
    board = new BoardRenderer(8, 8, 1.0f);

    // Create our gameWorld (no immediate spawn)
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

    // We differentiate whether the player is carrying the model (no mouse hold needed)
    bool carryingModel = false;

    int carryingPokemonIndex = -1;
    bool panningCamera = false;

    // Keep track of last mouse position for panning
    int lastMouseX = 0;
    int lastMouseY = 0;

    // Use a small radius to detect clicks on the model
    float pickRadius = 0.7f;

    SDL_Event event;

    // Lambda to convert screen coordinates to a world position (plane y=0).
    auto screenToWorld = [&](int mouseX, int mouseY) -> glm::vec3 {
        glm::vec4 viewport(0.0f, 0.0f, WIDTH, HEIGHT);
        float winX = static_cast<float>(mouseX);
        float winY = static_cast<float>(HEIGHT - mouseY); // invert Y
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

            // Mouse wheel zoom
            if (event.type == SDL_MOUSEWHEEL) {
                camera->zoom(event.wheel.y * 0.5f);
            }

            // Pass events to the state manager (for StarterSelectionState, etc.)
            if (stateManager) {
                stateManager->handleInput(event);
            }

            // Only do picking or camera panning if not in StarterSelectionState
            if (!dynamic_cast<StarterSelectionState*>(stateManager->getCurrentState())) {
                
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                    
                    // Convert click to 3D
                    glm::vec3 clickPos3D = screenToWorld(event.button.x, event.button.y);

                    if (carryingPokemonIndex < 0) {
                        // Not carrying any Pokemon => see if we clicked near one
                        
                        // We'll do a simple loop over all pokemons in gameWorld
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

                        // If the closest is within pickRadius, pick it up
                        if (closestIndex >= 0 && closestDist <= pickRadius) {
                            carryingPokemonIndex = closestIndex;
                            std::cout << "[Event] Picked up pokemon index " << closestIndex << "\n";
                        } else {
                            // Start panning camera
                            panningCamera = true;
                            lastMouseX = event.button.x;
                            lastMouseY = event.button.y;
                        }
                    } else {
                        // We are carrying a pokemon => place it
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
                        // Assume board cell size is 1.0 and board dimensions are known.
                        const float minX = -3.5f;
                        const float maxX = 3.5f;
                        const float minZ = 0.5f;
                        const float maxZ = 3.5f;

                        glm::vec3 newPos = screenToWorld(event.motion.x, event.motion.y);
                        newPos.x = std::floor(newPos.x) + 0.5f;
                        newPos.z = std::floor(newPos.z) + 0.5f;
                        newPos.y = 0.0f;

                        // Clamp the new position to ensure it stays on the board and on the correct side.
                        newPos.x = glm::clamp(newPos.x, minX, maxX);
                        newPos.z = glm::clamp(newPos.z, minZ, maxZ);

                        // Update the position for the carried Pokémon instance.
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

        // Calculate deltaTime for any updates
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // Update the active game state
        if (stateManager) {
            stateManager->update(deltaTime);
        }

        // Optional: any extra global updates
        update();

        // WASD/Arrow keys to move camera 
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

        // Render logic
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        board->draw(*camera);
        
        // Draw all pokemons via gameWorld
        gameWorld->drawAll(*camera);

        // Render any UI from the active state
        if (stateManager) {
            stateManager->render();
        }

        SDL_GL_SwapWindow(window);

        // FPS tracking
        frameCount++;
        if (deltaTime >= 1.0f) {
            std::cout << "[FPS] " << frameCount << " frames/sec\n";
            frameCount = 0;
        }
    }
}

void Application::update() {
    // Game logic here; for now, nothing happens.
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
    // Now we also delete gameWorld
    if (gameWorld) {
        delete gameWorld;
    }
    if (stateManager) {
        delete stateManager;
    }
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "[Shutdown] Shutdown complete.\n";
}

