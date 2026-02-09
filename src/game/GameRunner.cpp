// src/game/GameRunner.cpp

#include "game/GameRunner.h"

#include "game/GameApp.h"

#include "engine/core/EngineServices.h"
#include "engine/core/GameContext.h"
#include "engine/core/GameLoop.h"
#include "engine/events/EventBus.h"
#include "engine/input/InputEvent.h"
#include "engine/input/SdlKeyMap.h"
#include "engine/platform/Window.h"
#include "engine/render/Camera3D.h"
#include "engine/render/Renderer.h"
#include "engine/ui/BootLoadingView.h"
#include "engine/utils/ResourceManager.h"
#include "engine/utils/ShaderCache.h"

#define NOMINMAX
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <glad/glad.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

namespace {
    constexpr unsigned int START_W  = 1280;
    constexpr unsigned int START_H  = 720;
    constexpr float TIME_STEP = 1.0f / 60.0f;

    int scaledMouseX(int x, float s) { return (int)std::lround((float)x * s); }
    int scaledMouseY(int y, float s) { return (int)std::lround((float)y * s); }

    InputEvent::MouseButton mapSdlMouseButtonToEngineButton(int sdlButton) {
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

    bool translateSdlToInputEvent(
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
                out = InputEvent::KeyDownEvent(engine::input::mapSdlKeyToEngineKey((int)sdl.key.keysym.sym), sdl.key.repeat != 0);
                return true;

            case SDL_KEYUP:
                out = InputEvent::KeyUpEvent(engine::input::mapSdlKeyToEngineKey((int)sdl.key.keysym.sym));
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

    class GameRunner {
    public:
        bool init();
        int run(GameLoop& game);
        void shutdown();

    private:
        void updateDrawableSizeAndViewport();
        void updateMouseScale();
        void updateCameraAspect();
        bool applyVideoMode(int width, int height, bool fullscreenWanted);
        GameContext::VideoMode queryVideoMode() const;

        void setTitle(const std::string& title);
        void swapBuffers();
        bool pumpPreloadEvents();
        void renderBootLoading(float progress01);

    private:
        std::unique_ptr<Window>   window;
        std::unique_ptr<Renderer> renderer;
        std::unique_ptr<Camera3D> camera;

        std::unique_ptr<BootLoadingView> bootLoadingView;

        ResourceManager resourceManager;
        ShaderCache shaderCache;
        EventBus eventBus;
        EngineServices services;

        bool initialized = false;

        int drawableW = (int)START_W;
        int drawableH = (int)START_H;

        int windowW = (int)START_W;
        int windowH = (int)START_H;
        bool fullscreen = false;

        float mouseScaleX = 1.0f;
        float mouseScaleY = 1.0f;
    };

    bool GameRunner::init() {
        try {
            window = std::make_unique<Window>("Pokemon Autochess", (int)START_W, (int)START_H);
        } catch (const std::exception& ex) {
            std::cerr << "[GameRunner] Window init failed: " << ex.what() << "\n";
            return false;
        }

        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            std::cerr << "[GameRunner] Failed to initialize GLAD\n";
            return false;
        }

        if (TTF_Init() == -1) {
            std::cerr << "[GameRunner] TTF_Init error: " << TTF_GetError() << "\n";
        }

        updateDrawableSizeAndViewport();
        updateMouseScale();
        const Uint32 flags = SDL_GetWindowFlags(window->getSDLWindow());
        fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0 || (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;

        glEnable(GL_DEPTH_TEST);

        bootLoadingView = std::make_unique<BootLoadingView>();
        bootLoadingView->init(shaderCache);

        setTitle("PokemonAutochess - Loading...");
        glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        swapBuffers();
        pumpPreloadEvents();

        renderer = std::make_unique<Renderer>();
        camera   = std::make_unique<Camera3D>(45.0f, float(drawableW) / float(drawableH), 0.1f, 100.0f);

        initialized = true;
        std::cout << "[Init] Game runner initialized.\n";
        return true;
    }

    void GameRunner::shutdown() {
        std::cout << "[Shutdown] Game runner...\n";

        if (renderer) {
            renderer->shutdown();
            renderer.reset();
        }

        shaderCache.clear();

        resourceManager.clear();
        bootLoadingView.reset();
        camera.reset();

        window.reset();

        if (SDL_WasInit(SDL_INIT_EVERYTHING) != 0) {
            TTF_Quit();
            SDL_Quit();
        }

        initialized = false;
        std::cout << "[Shutdown] Game runner done.\n";
    }

    void GameRunner::updateDrawableSizeAndViewport() {
        if (!window || !window->getSDLWindow()) return;

        SDL_GetWindowSize(window->getSDLWindow(), &windowW, &windowH);
        SDL_GL_GetDrawableSize(window->getSDLWindow(), &drawableW, &drawableH);

        if (drawableW <= 0) drawableW = windowW;
        if (drawableH <= 0) drawableH = windowH;

        glViewport(0, 0, drawableW, drawableH);
    }

    void GameRunner::updateMouseScale() {
        if (windowW > 0 && windowH > 0) {
            mouseScaleX = (float)drawableW / (float)windowW;
            mouseScaleY = (float)drawableH / (float)windowH;
        } else {
            mouseScaleX = 1.0f;
            mouseScaleY = 1.0f;
        }
    }

    void GameRunner::updateCameraAspect() {
        if (camera && drawableW > 0 && drawableH > 0) {
            camera->setAspectRatio(float(drawableW) / float(drawableH));
        }
    }

    bool GameRunner::applyVideoMode(int width, int height, bool fullscreenWanted) {
        if (!window || !window->getSDLWindow()) return false;
        SDL_Window* sdlWindow = window->getSDLWindow();

        width = std::max(640, width);
        height = std::max(360, height);

        if (fullscreenWanted) {
            SDL_DisplayMode mode{};
            mode.w = width;
            mode.h = height;
            mode.format = SDL_PIXELFORMAT_UNKNOWN;
            mode.refresh_rate = 0;
            mode.driverdata = nullptr;
            if (SDL_SetWindowDisplayMode(sdlWindow, &mode) != 0) {
                std::cerr << "[Video] SDL_SetWindowDisplayMode failed: " << SDL_GetError() << "\n";
            }
            if (SDL_SetWindowFullscreen(sdlWindow, SDL_WINDOW_FULLSCREEN) != 0) {
                std::cerr << "[Video] SDL_WINDOW_FULLSCREEN failed, trying desktop: " << SDL_GetError() << "\n";
                if (SDL_SetWindowFullscreen(sdlWindow, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
                    std::cerr << "[Video] SDL_WINDOW_FULLSCREEN_DESKTOP failed: " << SDL_GetError() << "\n";
                    return false;
                }
            }
            fullscreen = true;
        } else {
            if (SDL_SetWindowFullscreen(sdlWindow, 0) != 0) {
                std::cerr << "[Video] Exiting fullscreen failed: " << SDL_GetError() << "\n";
                return false;
            }
            SDL_SetWindowSize(sdlWindow, width, height);
            SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            fullscreen = false;
        }

        updateDrawableSizeAndViewport();
        updateMouseScale();
        updateCameraAspect();
        return true;
    }

    GameContext::VideoMode GameRunner::queryVideoMode() const {
        GameContext::VideoMode mode;
        mode.width = std::max(1, drawableW);
        mode.height = std::max(1, drawableH);
        mode.fullscreen = fullscreen;
        return mode;
    }

    void GameRunner::setTitle(const std::string& title) {
        if (window) window->setTitle(title);
    }

    void GameRunner::swapBuffers() {
        if (window) window->swapBuffers();
    }

    bool GameRunner::pumpPreloadEvents() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) return false;

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    updateDrawableSizeAndViewport();
                    updateMouseScale();
                    updateCameraAspect();
                }
            }
        }
        return true;
    }

    void GameRunner::renderBootLoading(float progress01) {
        if (!bootLoadingView) return;
        updateDrawableSizeAndViewport();
        bootLoadingView->render(progress01, drawableW, drawableH);
        swapBuffers();
    }

    int GameRunner::run(GameLoop& game) {
        if (!initialized) return 1;

        std::cout << "[Run] Main loop @ 60 Hz...\n";

        bool running = true;
        services.resources = &resourceManager;
        services.shaders = &shaderCache;
        services.events = &eventBus;

        GameContext ctx;
        ctx.renderer = renderer.get();
        ctx.camera   = camera.get();
        ctx.services = &services;
        ctx.drawableW = drawableW;
        ctx.drawableH = drawableH;

        ctx.setTitle = [this](const std::string& t) { this->setTitle(t); };
        ctx.swapBuffers = [this]() { this->swapBuffers(); };
        ctx.requestQuit = [&running]() { running = false; };
        ctx.pumpPreloadEvents = [this]() { return this->pumpPreloadEvents(); };
        ctx.renderBootLoading = [this](float p) { this->renderBootLoading(p); };
        ctx.applyVideoMode = [this, &ctx](int width, int height, bool isFullscreen) {
            const bool ok = this->applyVideoMode(width, height, isFullscreen);
            ctx.drawableW = drawableW;
            ctx.drawableH = drawableH;
            return ok;
        };
        ctx.queryVideoMode = [this]() { return this->queryVideoMode(); };

        game.init(ctx);

        if (!running) {
            game.shutdown();
            return 0;
        }

        using clock = std::chrono::high_resolution_clock;
        auto previous = clock::now();
        double accumulator = 0.0;

        int frameCount = 0;
        double fpsTimer = 0.0;

        while (running) {
            SDL_Event sdlEvent;

            while (SDL_PollEvent(&sdlEvent)) {
                if (sdlEvent.type == SDL_QUIT) {
                    running = false;
                }

                if (sdlEvent.type == SDL_WINDOWEVENT) {
                    if (sdlEvent.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                        sdlEvent.window.event == SDL_WINDOWEVENT_RESIZED) {
                        updateDrawableSizeAndViewport();
                        updateMouseScale();
                        updateCameraAspect();

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
        return 0;
    }
} // namespace

namespace game {

int runGame() {
    GameRunner runner;
    if (!runner.init()) {
        runner.shutdown();
        return 1;
    }

    GameApp app;
    const int result = runner.run(app);

    runner.shutdown();
    return result;
}

} // namespace game
