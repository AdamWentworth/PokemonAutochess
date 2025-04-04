// Application.cpp

#include "Application.h"
#include "../render/Renderer.h"
#include "../render/BoardRenderer.h"
#include "../render/Model.h"

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

SDL_Window* window = nullptr;
SDL_GLContext glContext = nullptr;
BoardRenderer* board = nullptr;
Model* bulbasaurModel = nullptr;

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

    camera = new Camera3D(45.0f, (float)WIDTH / HEIGHT, 0.1f, 100.0f);

    board = new BoardRenderer(8, 8, 1.0f);

    bulbasaurModel = new Model("assets/models/bulbasaur.glb");

    // Optionally, print a log indicating successful renderer initialization.
    std::cout << "[Init] Renderer initialized successfully.\n";
}

void Application::run() {
    std::cout << "[Run] Entering main loop...\n";
    auto lastTime = std::chrono::high_resolution_clock::now();
    int frameCount = 0;
    bool running = true;
    SDL_Event event;

    bool dragging = false;
    int lastMouseX = 0, lastMouseY = 0;

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
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                dragging = true;
                lastMouseX = event.button.x;
                lastMouseY = event.button.y;
            }
            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                dragging = false;
            }            
            if (event.type == SDL_MOUSEMOTION && dragging) {
                int dx = event.motion.x - lastMouseX;
                int dy = event.motion.y - lastMouseY;
                lastMouseX = event.motion.x;
                lastMouseY = event.motion.y;
            
                float sensitivity = 0.015f;
            
                // Match arrow key behavior: move along world XZ plane
                glm::vec3 dragMove(
                    static_cast<float>(-dx) * sensitivity, // move left-right
                    0.0f,
                    static_cast<float>(-dy) * sensitivity   // move forward-back
                );
            
                camera->move(dragMove);
            }            
        }

        update();

        const Uint8* keystates = SDL_GetKeyboardState(nullptr);
        float cameraSpeed = 0.1f;

        glm::vec3 moveDir(0.0f);

        // WASD / Arrow key panning
        if (keystates[SDL_SCANCODE_W] || keystates[SDL_SCANCODE_UP])    moveDir.z -= cameraSpeed;
        if (keystates[SDL_SCANCODE_S] || keystates[SDL_SCANCODE_DOWN])  moveDir.z += cameraSpeed;
        if (keystates[SDL_SCANCODE_A] || keystates[SDL_SCANCODE_LEFT])  moveDir.x -= cameraSpeed;
        if (keystates[SDL_SCANCODE_D] || keystates[SDL_SCANCODE_RIGHT]) moveDir.x += cameraSpeed;

        if (glm::length(moveDir) > 0.0f) {
            camera->move(moveDir);
        }

        // Clear the screen to a dark gray color
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Draw the triangle
        board->draw(*camera);

        bulbasaurModel->draw(*camera);

        SDL_GL_SwapWindow(window);

        // FPS tracking (log once per second)
        frameCount++;
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        if (deltaTime >= 1.0f) {
            std::cout << "[FPS] " << frameCount << " frames/sec\n";
            frameCount = 0;
            lastTime = now;
        }

        // (Optional) Slow down the loop a bit for debugging
        // std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

void Application::update() {
    // Game logic here; for now, nothing happens.
}

void Application::shutdown() {
    std::cout << "[Shutdown] Shutting down renderer...\n";
    if (renderer) {
        renderer->shutdown();
        delete renderer;
    }
    if (camera) delete camera;
    if (board) {
        board->shutdown();
        delete board;
    }
    if (bulbasaurModel) delete bulbasaurModel;
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "[Shutdown] Shutdown complete.\n";
}
