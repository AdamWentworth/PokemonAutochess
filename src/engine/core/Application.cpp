// src/engine/core/Application.cpp

#include "engine/core/Application.h"
#include "engine/core/Window.h"

#include "engine/core/GameLoop.h"
#include "engine/core/GameContext.h"

#include "engine/input/InputEvent.h"

#include "engine/render/Renderer.h"
#include "engine/ui/BootLoadingView.h"

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
#include <algorithm>
#include <cmath>

namespace {
    constexpr unsigned int START_W  = 1280;
    constexpr unsigned int START_H  = 720;

    static int scaledMouseX(int x, float s) { return (int)std::lround((float)x * s); }
    static int scaledMouseY(int y, float s) { return (int)std::lround((float)y * s); }

    // Returns true if we produced an InputEvent for the game.
    static bool translateSdlToInputEvent(
        const SDL_Event& sdl,
        float mouseScaleX, float mouseScaleY,
        int windowW, int windowH,
        int drawableW, int drawableH,
        InputEvent& out
    ) {
        switch (sdl.type) {
            case SDL_QUIT:
                out = InputEvent::QuitEvent();
                return true;

            case SDL_WINDOWEVENT:
                if (sdl.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    sdl.window.event == SDL_WINDOWEVENT_RESIZED) {
                    out = InputEvent::ResizeEvent(windowW, windowH, drawableW, drawableH);
                    return true;
                }
                return false;

            case SDL_KEYDOWN:
                out = InputEvent::KeyDownEvent((int)sdl.key.keysym.sym, sdl.key.repeat != 0);
                return true;

            case SDL_KEYUP:
                out = InputEvent::KeyUpEvent((int)sdl.key.keysym.sym);
                return true;

            case SDL_MOUSEMOTION: {
                int mx = scaledMouseX(sdl.motion.x, mouseScaleX);
                int my = scaledMouseY(sdl.motion.y, mouseScaleY);
                out = InputEvent::MouseMoveEvent(mx, my);
                return true;
            }

            case SDL_MOUSEBUTTONDOWN: {
                int mx = scaledMouseX(sdl.button.x, mouseScaleX);
                int my = scaledMouseY(sdl.button.y, mouseScaleY);
                out = InputEvent::MouseDownEvent(mx, my, (int)sdl.button.button);
                return true;
            }

            case SDL_MOUSEBUTTONUP: {
                int mx = scaledMouseX(sdl.button.x, mouseScaleX);
                int my = scaledMouseY(sdl.button.y, mouseScaleY);
                out = InputEvent::MouseUpEvent(mx, my, (int)sdl.button.button);
                return true;
            }

            case SDL_MOUSEWHEEL:
                out = InputEvent::MouseWheelEvent((int)sdl.wheel.x, (int)sdl.wheel.y);
                return true;

            default:
                return false;
        }
    }
}

Application::Application() {
    initApplication();
}

Application::~Application() {
    shutdownApplication();
}

void Application::initApplication() {
    if (TTF_Init() == -1) {
        std::cerr << "[Application] TTF_Init error: " << TTF_GetError() << "\n";
    }

    window = std::make_unique<Window>("Pokemon Autochess", (int)START_W, (int)START_H);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        std::exit(EXIT_FAILURE);
    }

    updateDrawableSizeAndViewport();
    updateMouseScale();

    glEnable(GL_DEPTH_TEST);

    bootLoadingView = std::make_unique<BootLoadingView>();
    bootLoadingView->init();

    // show a first frame so the window looks alive
    setTitle("PokemonAutochess - Loading...");
    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    swapBuffers();
    pumpPreloadEvents();

    renderer = std::make_unique<Renderer>();
    camera   = std::make_unique<Camera3D>(45.0f, float(drawableW) / float(drawableH), 0.1f, 100.0f);

    std::cout << "[Init] Application initialized.\n";
}

void Application::shutdownApplication() {
    std::cout << "[Shutdown] Application...\n";

    if (renderer) {
        renderer->shutdown();
        renderer.reset();
    }

    camera.reset();
    bootLoadingView.reset();
    window.reset();

    TTF_Quit();

    std::cout << "[Shutdown] Application done.\n";
}

void Application::updateDrawableSizeAndViewport() {
    if (!window || !window->getSDLWindow()) return;

    SDL_GetWindowSize(window->getSDLWindow(), &windowW, &windowH);
    SDL_GL_GetDrawableSize(window->getSDLWindow(), &drawableW, &drawableH);

    if (drawableW <= 0) drawableW = windowW;
    if (drawableH <= 0) drawableH = windowH;

    glViewport(0, 0, drawableW, drawableH);
}

void Application::updateMouseScale() {
    if (windowW > 0 && windowH > 0) {
        mouseScaleX = (float)drawableW / (float)windowW;
        mouseScaleY = (float)drawableH / (float)windowH;
    } else {
        mouseScaleX = 1.0f;
        mouseScaleY = 1.0f;
    }
}

void Application::setTitle(const std::string& title) {
    if (window) window->setTitle(title);
}

void Application::swapBuffers() {
    if (window) window->swapBuffers();
}

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

                // keep camera aspect correct
                if (camera && drawableW > 0 && drawableH > 0) {
                    *camera = Camera3D(45.0f, float(drawableW) / float(drawableH), 0.1f, 100.0f);
                }
            }
        }
    }
    return true;
}

void Application::renderBootLoading(float progress01) {
    if (!bootLoadingView) return;
    updateDrawableSizeAndViewport();
    bootLoadingView->render(progress01, drawableW, drawableH);
    swapBuffers();
}

void Application::run(GameLoop& game) {
    std::cout << "[Run] Main loop @ 60 Hz...\n";

    GameContext ctx;
    ctx.renderer = renderer.get();
    ctx.camera   = camera.get();
    ctx.drawableW = drawableW;
    ctx.drawableH = drawableH;

    // Bind engine helpers as callbacks (stable contract)
    ctx.setTitle = [this](const std::string& t) { this->setTitle(t); };
    ctx.swapBuffers = [this]() { this->swapBuffers(); };
    ctx.pumpPreloadEvents = [this]() { return this->pumpPreloadEvents(); };
    ctx.renderBootLoading = [this](float p) { this->renderBootLoading(p); };

    game.init(ctx);

    using clock = std::chrono::high_resolution_clock;
    auto previous = clock::now();
    double accumulator = 0.0;

    int frameCount = 0;
    static double fpsTimer = 0.0;

    bool running = true;

    while (running) {
        SDL_Event sdlEvent;

        while (SDL_PollEvent(&sdlEvent)) {
            if (sdlEvent.type == SDL_QUIT ||
                (sdlEvent.type == SDL_KEYDOWN && sdlEvent.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }

            // resize handling (engine-owned)
            if (sdlEvent.type == SDL_WINDOWEVENT) {
                if (sdlEvent.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    sdlEvent.window.event == SDL_WINDOWEVENT_RESIZED) {
                    updateDrawableSizeAndViewport();
                    updateMouseScale();

                    if (camera) {
                        *camera = Camera3D(45.0f, float(drawableW) / float(drawableH), 0.1f, 100.0f);
                    }

                    // keep context size in sync
                    ctx.drawableW = drawableW;
                    ctx.drawableH = drawableH;
                }
            }

            // Engine-level mouse events -> emit engine events (UI + picking)

            // Convert SDL -> InputEvent and hand to game (SDL-free boundary)
            InputEvent e;
            if (translateSdlToInputEvent(
                    sdlEvent,
                    mouseScaleX, mouseScaleY,
                    windowW, windowH,
                    drawableW, drawableH,
                    e
                )) {
                game.handleEvent(e);
            }
        }

        auto now = clock::now();
        double frameDt = std::chrono::duration<double>(now - previous).count();
        frameDt = std::min(frameDt, 0.25);
        previous = now;

        accumulator += frameDt;

        while (accumulator >= TIME_STEP) {
            game.fixedUpdate(TIME_STEP);
            accumulator -= TIME_STEP;
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        game.render(drawableW, drawableH);

        SDL_GL_SwapWindow(window->getSDLWindow());

        frameCount++;
        fpsTimer += frameDt;
        if (fpsTimer >= 1.0) {
            std::cout << "[FPS] " << frameCount << "\n";
            frameCount = 0;
            fpsTimer = 0.0;
        }
    }

    game.shutdown();
}
