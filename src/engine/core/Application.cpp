// src/engine/core/Application.cpp

#include "engine/core/Application.h"
#include "engine/core/Window.h"

#include "engine/core/GameLoop.h"
#include "engine/core/GameContext.h"
#include "engine/core/EngineServices.h"

#include "engine/input/InputEvent.h"

#include "engine/render/Renderer.h"
#include "engine/ui/BootLoadingView.h"

#include "engine/utils/ResourceManager.h"
#include "engine/utils/ShaderLibrary.h"
#include "engine/core/SystemRegistry.h"

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

    static InputEvent::Key mapSdlKeyToEngineKey(int sdlKeycode) {
        switch (sdlKeycode) {
            case SDLK_0: return InputEvent::Key::Num0;
            case SDLK_1: return InputEvent::Key::Num1;
            case SDLK_2: return InputEvent::Key::Num2;
            case SDLK_3: return InputEvent::Key::Num3;
            case SDLK_4: return InputEvent::Key::Num4;
            case SDLK_5: return InputEvent::Key::Num5;
            case SDLK_6: return InputEvent::Key::Num6;
            case SDLK_7: return InputEvent::Key::Num7;
            case SDLK_8: return InputEvent::Key::Num8;
            case SDLK_9: return InputEvent::Key::Num9;

            case SDLK_a: return InputEvent::Key::A;
            case SDLK_b: return InputEvent::Key::B;
            case SDLK_c: return InputEvent::Key::C;
            case SDLK_d: return InputEvent::Key::D;
            case SDLK_e: return InputEvent::Key::E;
            case SDLK_f: return InputEvent::Key::F;
            case SDLK_g: return InputEvent::Key::G;
            case SDLK_h: return InputEvent::Key::H;
            case SDLK_i: return InputEvent::Key::I;
            case SDLK_j: return InputEvent::Key::J;
            case SDLK_k: return InputEvent::Key::K;
            case SDLK_l: return InputEvent::Key::L;
            case SDLK_m: return InputEvent::Key::M;
            case SDLK_n: return InputEvent::Key::N;
            case SDLK_o: return InputEvent::Key::O;
            case SDLK_p: return InputEvent::Key::P;
            case SDLK_q: return InputEvent::Key::Q;
            case SDLK_r: return InputEvent::Key::R;
            case SDLK_s: return InputEvent::Key::S;
            case SDLK_t: return InputEvent::Key::T;
            case SDLK_u: return InputEvent::Key::U;
            case SDLK_v: return InputEvent::Key::V;
            case SDLK_w: return InputEvent::Key::W;
            case SDLK_x: return InputEvent::Key::X;
            case SDLK_y: return InputEvent::Key::Y;
            case SDLK_z: return InputEvent::Key::Z;

            case SDLK_ESCAPE: return InputEvent::Key::Escape;
            case SDLK_RETURN: return InputEvent::Key::Enter;
            case SDLK_SPACE:  return InputEvent::Key::Space;
            case SDLK_TAB:    return InputEvent::Key::Tab;
            case SDLK_BACKSPACE: return InputEvent::Key::Backspace;

            case SDLK_LEFT:  return InputEvent::Key::Left;
            case SDLK_RIGHT: return InputEvent::Key::Right;
            case SDLK_UP:    return InputEvent::Key::Up;
            case SDLK_DOWN:  return InputEvent::Key::Down;

            case SDLK_LSHIFT: return InputEvent::Key::LShift;
            case SDLK_RSHIFT: return InputEvent::Key::RShift;
            case SDLK_LCTRL:  return InputEvent::Key::LCtrl;
            case SDLK_RCTRL:  return InputEvent::Key::RCtrl;
            case SDLK_LALT:   return InputEvent::Key::LAlt;
            case SDLK_RALT:   return InputEvent::Key::RAlt;

            default:
                return InputEvent::Key::Unknown;
        }
    }

    static InputEvent::MouseButton mapSdlMouseButtonToEngineButton(int sdlButton) {
        switch (sdlButton) {
            case SDL_BUTTON_LEFT:   return InputEvent::MouseButton::Left;
            case SDL_BUTTON_MIDDLE: return InputEvent::MouseButton::Middle;
            case SDL_BUTTON_RIGHT:  return InputEvent::MouseButton::Right;
            case SDL_BUTTON_X1:     return InputEvent::MouseButton::X1;
            case SDL_BUTTON_X2:     return InputEvent::MouseButton::X2;
            default:
                return InputEvent::MouseButton::Unknown;
        }
    }

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
                out = InputEvent::KeyDownEvent(mapSdlKeyToEngineKey((int)sdl.key.keysym.sym), sdl.key.repeat != 0);
                return true;

            case SDL_KEYUP:
                out = InputEvent::KeyUpEvent(mapSdlKeyToEngineKey((int)sdl.key.keysym.sym));
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
                out = InputEvent::MouseDownEvent(mx, my, mapSdlMouseButtonToEngineButton((int)sdl.button.button));
                return true;
            }

            case SDL_MOUSEBUTTONUP: {
                int mx = scaledMouseX(sdl.button.x, mouseScaleX);
                int my = scaledMouseY(sdl.button.y, mouseScaleY);
                out = InputEvent::MouseUpEvent(mx, my, mapSdlMouseButtonToEngineButton((int)sdl.button.button));
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
    // Window ctor performs SDL_Init + creates GL context.
    window = std::make_unique<Window>("Pokemon Autochess", (int)START_W, (int)START_H);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        std::exit(EXIT_FAILURE);
    }

    // Wire engine-owned shader cache BEFORE anything calls ShaderLibrary::get()
    ShaderLibrary::setCache(&shaderCache);

    // TTF depends on SDL being initialized (now true because Window ctor did SDL_Init).
    if (TTF_Init() == -1) {
        std::cerr << "[Application] TTF_Init error: " << TTF_GetError() << "\n";
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

    // 1) Destroy anything that might issue GL calls while the context is still alive.
    if (renderer) {
        renderer->shutdown();
        renderer.reset();
    }

    // 2) Force-release GL-backed caches BEFORE destroying the window/context.
    // Shader programs
    ShaderLibrary::clear();
    ShaderLibrary::setCache(nullptr);
    shaderCache.clear();

    // Models / textures / meshes managed by ResourceManager
    resourceManager.clear();

    // If systems can own GL resources, clear them while context exists.
    systemRegistry.clear();

    // Boot loading view contains VAO/VBO -> destroy while GL context exists
    bootLoadingView.reset();
    camera.reset();

    // 3) Destroy the GL context + window
    window.reset();

    // 4) Tear down SDL subsystems last
    TTF_Quit();
    SDL_Quit();

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

    bool running = true;

    EngineServices services;
    services.systems = &systemRegistry;
    services.resources = &resourceManager;

    services.shaders = &shaderCache;

    GameContext ctx;
    ctx.renderer = renderer.get();
    ctx.camera   = camera.get();
    ctx.services = &services;
    ctx.drawableW = drawableW;
    ctx.drawableH = drawableH;

    // Bind engine helpers as callbacks (stable contract)
    ctx.setTitle = [this](const std::string& t) { this->setTitle(t); };
    ctx.swapBuffers = [this]() { this->swapBuffers(); };
    ctx.requestQuit = [&running]() { running = false; };
    ctx.pumpPreloadEvents = [this]() { return this->pumpPreloadEvents(); };
    ctx.renderBootLoading = [this](float p) { this->renderBootLoading(p); };

    game.init(ctx);

    // If the game requested quit during init (e.g. user closed during preload), exit cleanly.
    if (!running) {
        game.shutdown();
        return;
    }

    using clock = std::chrono::high_resolution_clock;
    auto previous = clock::now();
    double accumulator = 0.0;

    int frameCount = 0;
    static double fpsTimer = 0.0;


    while (running) {
        SDL_Event sdlEvent;

        while (SDL_PollEvent(&sdlEvent)) {
            if (sdlEvent.type == SDL_QUIT ||
                (sdlEvent.type == SDL_KEYDOWN && sdlEvent.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }

            if (sdlEvent.type == SDL_WINDOWEVENT) {
                if (sdlEvent.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    sdlEvent.window.event == SDL_WINDOWEVENT_RESIZED) {
                    updateDrawableSizeAndViewport();
                    updateMouseScale();

                    if (camera && drawableW > 0 && drawableH > 0) {
                        *camera = Camera3D(45.0f, float(drawableW) / float(drawableH), 0.1f, 100.0f);
                    }

                    ctx.drawableW = drawableW;
                    ctx.drawableH = drawableH;
                }
            }

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

        swapBuffers();

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