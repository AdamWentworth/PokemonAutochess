// src/game/GameRunner.cpp

#include "game/runtime/GameRunner.h"

#include "game/runtime/GameApp.h"

#include "engine/core/EngineServices.h"
#include "engine/core/Environment.h"
#include "engine/core/GameContext.h"
#include "engine/core/GameLoop.h"
#include "engine/core/Paths.h"
#include "engine/events/EventBus.h"
#include "engine/input/InputEvent.h"
#include "engine/input/SdlKeyMap.h"
#include "engine/platform/Window.h"
#include "engine/render/Camera3D.h"
#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/IRenderBackend.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/ui/BootLoadingView.h"
#include "engine/utils/ResourceManager.h"
#include "engine/utils/ShaderCache.h"
#include "game/runtime/GpuAdapters.h"
#include "game/runtime/AutoQuitPolicy.h"
#include "game/runtime/VideoInitGuards.h"
#include "game/runtime/VideoPreferences.h"

#define NOMINMAX
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <glad/glad.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

namespace {
    constexpr unsigned int START_W  = 1280;
    constexpr unsigned int START_H  = 720;
    constexpr float TIME_STEP = 1.0f / 60.0f;

    int scaledMouseX(int x, float s) { return (int)std::lround((float)x * s); }
    int scaledMouseY(int y, float s) { return (int)std::lround((float)y * s); }

    std::string toLowerCopy(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    bool containsCi(const std::string& haystack, const std::string& needle) {
        const std::string h = toLowerCopy(haystack);
        const std::string n = toLowerCopy(needle);
        return h.find(n) != std::string::npos;
    }

    bool looksIntegratedGpu(const std::string& vendor, const std::string& renderer) {
        // Heuristic: Intel OpenGL contexts on hybrid laptops are typically iGPU.
        return containsCi(vendor, "intel") || containsCi(renderer, "intel");
    }

    struct StartupVideoOverride {
        bool hasWidth = false;
        bool hasHeight = false;
        bool hasFullscreen = false;
        int width = 0;
        int height = 0;
        bool fullscreen = false;

        bool enabled() const {
            return hasWidth || hasHeight || hasFullscreen;
        }
    };

    bool parseEnvIntValue(const char* envName, int& outValue) {
        const auto raw = engine::env::get(envName);
        if (!raw.has_value()) return false;
        try {
            const long long parsed = std::stoll(*raw);
            if (parsed < static_cast<long long>(std::numeric_limits<int>::min()) ||
                parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
                std::cerr << "[Video] Ignoring out-of-range " << envName << " value: " << *raw << "\n";
                return false;
            }
            outValue = static_cast<int>(parsed);
            return true;
        } catch (...) {
            std::cerr << "[Video] Ignoring invalid " << envName << " value: " << *raw << "\n";
            return false;
        }
    }

    bool parseEnvBoolValue(const char* envName, bool& outValue) {
        const auto raw = engine::env::get(envName);
        if (!raw.has_value()) return false;
        const std::string token = toLowerCopy(*raw);
        if (token == "1" || token == "true" || token == "on" || token == "yes") {
            outValue = true;
            return true;
        }
        if (token == "0" || token == "false" || token == "off" || token == "no") {
            outValue = false;
            return true;
        }
        std::cerr << "[Video] Ignoring invalid " << envName << " value: " << *raw << "\n";
        return false;
    }

    StartupVideoOverride readStartupVideoOverride() {
        StartupVideoOverride out;
        out.hasWidth = parseEnvIntValue("PAC_VIDEO_WIDTH", out.width);
        out.hasHeight = parseEnvIntValue("PAC_VIDEO_HEIGHT", out.height);
        out.hasFullscreen = parseEnvBoolValue("PAC_VIDEO_FULLSCREEN", out.fullscreen);
        return out;
    }

    std::unique_ptr<IRenderBackend> createRenderBackend(game::video::RendererBackend backend,
                                                        SDL_Window* sdlWindow,
                                                        int width,
                                                        int height,
                                                        const std::string& preferredAdapter,
                                                        std::string* outError) {
        try {
            switch (backend) {
            case game::video::RendererBackend::Auto:
            case game::video::RendererBackend::OpenGL:
                return std::make_unique<OpenGLRenderBackend>();
            case game::video::RendererBackend::D3D12:
                return std::make_unique<D3D12RenderBackend>(sdlWindow, width, height, preferredAdapter);
            case game::video::RendererBackend::Vulkan:
                if (outError) *outError = "Vulkan backend is not implemented.";
                return nullptr;
            default:
                if (outError) *outError = "Unknown renderer backend.";
                return nullptr;
            }
        } catch (const std::exception& e) {
            if (outError) *outError = e.what();
            return nullptr;
        }
    }

    Window::GraphicsApi graphicsApiForBackend(game::video::RendererBackend backend) {
        switch (backend) {
        case game::video::RendererBackend::Auto:
        case game::video::RendererBackend::OpenGL:
            return Window::GraphicsApi::OpenGL;
        case game::video::RendererBackend::D3D12:
            return Window::GraphicsApi::Native;
        case game::video::RendererBackend::Vulkan:
            return Window::GraphicsApi::Native;
        default:
            return Window::GraphicsApi::OpenGL;
        }
    }

    const char* glStringOrUnknown(GLenum token) {
        const GLubyte* s = glGetString(token);
        return s ? reinterpret_cast<const char*>(s) : "<unknown>";
    }

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
        std::unique_ptr<IRenderBackend> renderer;
        std::unique_ptr<Camera3D> camera;

        std::unique_ptr<BootLoadingView> bootLoadingView;

        ResourceManager resourceManager;
        ShaderCache shaderCache;
        EventBus eventBus;
        EngineServices services;

        bool initialized = false;
        game::video::RendererBackend requestedBackend = game::video::RendererBackend::Auto;
        game::video::RendererBackend activeBackend = game::video::RendererBackend::OpenGL;

        int drawableW = (int)START_W;
        int drawableH = (int)START_H;

        int windowW = (int)START_W;
        int windowH = (int)START_H;
        bool fullscreen = false;
        bool windowHasOpenGLContext = false;
        bool glFunctionsReady = false;

        float mouseScaleX = 1.0f;
        float mouseScaleY = 1.0f;
    };

    bool GameRunner::init() {
        const std::string prefsPath = game::video::defaultPreferencesPath();
        game::video::Preferences prefs = game::video::loadPreferences(prefsPath);

        services.bootMenuScreen = prefs.bootMenuScreen;
        if (!prefs.bootMenuScreen.empty()) {
            prefs.bootMenuScreen.clear();
            std::string consumeErr;
            if (!game::video::savePreferences(prefs, prefsPath, &consumeErr)) {
                std::cerr << "[Video] Failed to clear one-shot boot menu screen: " << consumeErr << "\n";
            }
        }

        std::string backendToken = prefs.rendererBackend;
        if (const auto envBackend = engine::env::get("PAC_RENDER_BACKEND")) {
            backendToken = *envBackend;
            std::cout << "[Renderer] PAC_RENDER_BACKEND override: " << backendToken << "\n";
            std::cout << "[Renderer] Note: env override is active; saved Display settings "
                         "(Render API) are ignored until PAC_RENDER_BACKEND is unset.\n";
        }
        requestedBackend = game::video::parseRendererBackend(backendToken);
        services.requestedRendererBackend = game::video::rendererBackendName(requestedBackend);
        services.requireDiscreteGpu = prefs.requireDiscreteGpu;
        services.preferredGpuAdapter = prefs.preferredGpuAdapter;

        {
            const auto adapters = game::video::enumerateSystemGpuAdapters();
            services.availableGpuAdapters.clear();
            services.availableGpuAdapters.reserve(adapters.size());
            for (const auto& adapter : adapters) {
                services.availableGpuAdapters.push_back(adapter.name);
            }

            if (!adapters.empty()) {
                std::cout << "[GPU] Adapters detected: " << adapters.size() << "\n";
                for (std::size_t i = 0; i < adapters.size(); ++i) {
                    std::cout << "  [" << i << "] " << adapters[i].name
                              << " (" << (adapters[i].discrete ? "discrete" : "integrated") << ")\n";
                }
            } else {
                std::cout << "[GPU] Adapter enumeration unavailable for this platform/runtime.\n";
            }

            if (!services.preferredGpuAdapter.empty()) {
                const auto it = std::find_if(adapters.begin(), adapters.end(),
                    [this](const game::video::SystemGpuAdapter& adapter) {
                        return adapter.name == services.preferredGpuAdapter;
                    });
                if (it != adapters.end()) {
                    std::cout << "[GPU] Preferred adapter setting: " << services.preferredGpuAdapter << "\n";
                } else {
                    std::cout << "[GPU] Preferred adapter setting not found on this machine: "
                              << services.preferredGpuAdapter << "\n";
                }
            }
        }

        if (!game::video::isRendererBackendImplemented(requestedBackend)) {
            activeBackend = game::video::RendererBackend::OpenGL;
            services.rendererBackendFallback = true;
            services.rendererBackendFallbackReason =
                std::string("requested backend '") + services.requestedRendererBackend +
                "' is not implemented; falling back to OpenGL.";
            std::cout << "[Renderer] " << services.rendererBackendFallbackReason << "\n";
        } else if (requestedBackend == game::video::RendererBackend::Auto) {
            activeBackend = game::video::RendererBackend::OpenGL;
        } else {
            activeBackend = requestedBackend;
        }
        services.activeRendererBackend = game::video::rendererBackendName(activeBackend);

        try {
            window = std::make_unique<Window>(
                "Pokemon Autochess",
                static_cast<int>(START_W),
                static_cast<int>(START_H),
                graphicsApiForBackend(activeBackend));
        } catch (const std::exception& ex) {
            std::cerr << "[GameRunner] Window init failed: " << ex.what() << "\n";
            return false;
        }
        windowHasOpenGLContext = window->hasOpenGLContext();
        glFunctionsReady = false;

        updateDrawableSizeAndViewport();
        updateMouseScale();
        const Uint32 flags = SDL_GetWindowFlags(window->getSDLWindow());
        fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0 || (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;

        const StartupVideoOverride startupVideoOverride = readStartupVideoOverride();
        if (startupVideoOverride.enabled()) {
            const int overrideW = startupVideoOverride.hasWidth ? startupVideoOverride.width : windowW;
            const int overrideH = startupVideoOverride.hasHeight ? startupVideoOverride.height : windowH;
            const bool overrideFullscreen = startupVideoOverride.hasFullscreen
                ? startupVideoOverride.fullscreen
                : fullscreen;
            if (applyVideoMode(overrideW, overrideH, overrideFullscreen)) {
                std::cout << "[Video] Startup override applied: "
                          << (fullscreen ? "Fullscreen" : "Windowed")
                          << " " << drawableW << "x" << drawableH << "\n";
            } else {
                std::cerr << "[Video] Failed to apply startup override video mode.\n";
            }
        }

        if (windowHasOpenGLContext) {
            if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
                std::cerr << "[GameRunner] Failed to initialize GLAD\n";
                return false;
            }
            glFunctionsReady = true;

            bootLoadingView = std::make_unique<BootLoadingView>();
            bootLoadingView->init(shaderCache);

            setTitle("PokemonAutochess - Loading...");
            glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            swapBuffers();
            pumpPreloadEvents();
        } else {
            pumpPreloadEvents();
        }

        std::string backendCreateError;
        renderer = createRenderBackend(activeBackend,
                                       window ? window->getSDLWindow() : nullptr,
                                       drawableW,
                                       drawableH,
                                       services.preferredGpuAdapter,
                                       &backendCreateError);
        if (!renderer) {
            if (activeBackend != game::video::RendererBackend::OpenGL) {
                services.rendererBackendFallback = true;
                services.rendererBackendFallbackReason =
                    "backend '" + services.activeRendererBackend + "' failed to initialize (" + backendCreateError +
                    "); falling back to OpenGL.";
                std::cout << "[Renderer] " << services.rendererBackendFallbackReason << "\n";

                window.reset();
                try {
                    activeBackend = game::video::RendererBackend::OpenGL;
                    services.activeRendererBackend = game::video::rendererBackendName(activeBackend);
                    window = std::make_unique<Window>(
                        "Pokemon Autochess",
                        static_cast<int>(START_W),
                        static_cast<int>(START_H),
                        Window::GraphicsApi::OpenGL);
                    windowHasOpenGLContext = true;
                    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
                        std::cerr << "[GameRunner] Failed to initialize GLAD after fallback\n";
                        return false;
                    }
                    glFunctionsReady = true;
                    updateDrawableSizeAndViewport();
                    updateMouseScale();
                    renderer = createRenderBackend(activeBackend,
                                                   window ? window->getSDLWindow() : nullptr,
                                                   drawableW,
                                                   drawableH,
                                                   services.preferredGpuAdapter,
                                                   &backendCreateError);
                } catch (const std::exception& ex) {
                    std::cerr << "[Renderer] OpenGL fallback window init failed: " << ex.what() << "\n";
                    return false;
                }
            }
            if (!renderer) {
                std::cerr << "[Renderer] Failed to create backend '" << services.activeRendererBackend
                          << "' (" << backendCreateError << ").\n";
                return false;
            }
        }

        services.activeRendererBackend = renderer->backendId();
        services.gpuRenderer = renderer->activeGpuName();
        services.gpuDiscrete = renderer->activeGpuIsDiscrete();

        if (renderer->requiresOpenGLContext()) {
            services.gpuVendor = glStringOrUnknown(GL_VENDOR);
            if (services.gpuRenderer.empty()) {
                services.gpuRenderer = glStringOrUnknown(GL_RENDERER);
            }
            services.gpuDiscrete = !looksIntegratedGpu(services.gpuVendor, services.gpuRenderer);
        } else {
            services.gpuVendor = "d3d12";
            if (services.gpuRenderer.empty()) {
                services.gpuRenderer = "<unknown d3d12 adapter>";
            }
            std::cout << "[Renderer] D3D12 backend initialized with shared gameplay render path.\n";
        }

        std::cout << "[Renderer] Requested: " << services.requestedRendererBackend << "\n";
        std::cout << "[Renderer] Active:    " << services.activeRendererBackend << "\n";
        std::cout << "[GPU] Vendor:   " << services.gpuVendor << "\n";
        std::cout << "[GPU] Renderer: " << services.gpuRenderer << "\n";
        if (renderer->requiresOpenGLContext()) {
            std::cout << "[GPU] OpenGL:   " << glStringOrUnknown(GL_VERSION) << "\n";
            std::cout << "[GPU] GLSL:     " << glStringOrUnknown(GL_SHADING_LANGUAGE_VERSION) << "\n";
        }
        std::cout << "[GPU] Class:    " << (services.gpuDiscrete ? "discrete" : "integrated") << "\n";

        if (!services.preferredGpuAdapter.empty() &&
            !containsCi(services.gpuRenderer, services.preferredGpuAdapter)) {
            std::cout << "[GPU] Preferred adapter '" << services.preferredGpuAdapter
                      << "' was not selected by active backend.\n";
        }

        if (services.requireDiscreteGpu && !services.gpuDiscrete) {
            std::cerr << "[GPU] Discrete GPU required by settings, but integrated GPU is active.\n";
            std::cerr << "[GPU] Change Graphics preference to high performance or choose a discrete adapter.\n";
            return false;
        }

        if (TTF_Init() == -1) {
            std::cerr << "[GameRunner] TTF_Init error: " << TTF_GetError() << "\n";
        }

        camera   = std::make_unique<Camera3D>(45.0f, float(drawableW) / float(drawableH), 0.1f, 100.0f);
        if (renderer) {
            renderer->onResize(drawableW, drawableH);
        }

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
        if (windowHasOpenGLContext) {
            SDL_GL_GetDrawableSize(window->getSDLWindow(), &drawableW, &drawableH);
        } else {
            drawableW = windowW;
            drawableH = windowH;
        }

        if (drawableW <= 0) drawableW = windowW;
        if (drawableH <= 0) drawableH = windowH;

        if (game::runtime::video::shouldApplyOpenGLViewport(windowHasOpenGLContext, glFunctionsReady)) {
            glViewport(0, 0, drawableW, drawableH);
        }
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
        if (renderer) {
            renderer->onResize(drawableW, drawableH);
        }
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
        updateDrawableSizeAndViewport();
        if (bootLoadingView) {
            bootLoadingView->render(progress01, drawableW, drawableH);
            swapBuffers();
        } else if (renderer) {
            const float progress = std::clamp(progress01, 0.0f, 1.0f);
            renderer->beginFrame(0.05f, 0.05f, 0.07f, 1.0f);
            if (drawableW > 0 && drawableH > 0) {
                const float sw = static_cast<float>(drawableW);
                const float sh = static_cast<float>(drawableH);
                const float panelW = std::max(280.0f, sw * 0.42f);
                const float panelH = std::max(120.0f, sh * 0.20f);
                const float panelX = (sw - panelW) * 0.5f;
                const float panelY = (sh - panelH) * 0.5f;
                const float pad = std::max(10.0f, panelH * 0.16f);
                const float barW = std::max(120.0f, panelW - pad * 2.0f);
                const float barH = std::max(12.0f, panelH * 0.22f);
                const float barX = panelX + pad;
                const float barY = panelY + panelH - pad - barH;

                IRenderBackend::DebugQuad quads[5];
                quads[0] = IRenderBackend::DebugQuad{0.0f, 0.0f, sw, sh, 0.03f, 0.03f, 0.04f, 1.0f};
                quads[1] = IRenderBackend::DebugQuad{
                    panelX, panelY, panelW, panelH, 0.10f, 0.10f, 0.12f, 0.97f};
                quads[2] = IRenderBackend::DebugQuad{
                    panelX + 2.0f, panelY + 2.0f, panelW - 4.0f, panelH - 4.0f, 0.14f, 0.14f, 0.17f, 0.98f};
                quads[3] = IRenderBackend::DebugQuad{barX, barY, barW, barH, 0.22f, 0.22f, 0.26f, 1.0f};
                quads[4] = IRenderBackend::DebugQuad{
                    barX + 2.0f,
                    barY + 2.0f,
                    std::max(0.0f, (barW - 4.0f) * progress),
                    std::max(0.0f, barH - 4.0f),
                    0.77f,
                    0.77f,
                    0.81f,
                    1.0f};
                renderer->drawDebugQuads(quads, 5, drawableW, drawableH);
            }
            renderer->endFrame();
        }
    }

    int GameRunner::run(GameLoop& game) {
        if (!initialized) return 1;

        std::cout << "[Run] Main loop @ 60 Hz...\n";

        bool running = true;
        std::string stopReason;
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
        ctx.requestQuit = [&running, &stopReason]() {
            running = false;
            if (stopReason.empty()) stopReason = "requestQuit() callback invoked";
        };
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
        double perfAccumFrameMs = 0.0;
        double perfAccumFixedMs = 0.0;
        double perfAccumRenderBuildMs = 0.0;
        double perfAccumRenderSubmitMs = 0.0;
        double perfAccumPresentWaitMs = 0.0;
        double perfAccumLegacyRenderMs = 0.0;
        double perfAccumLegacySwapMs = 0.0;
        double perfAccumGpuFrameMs = 0.0;
        int perfAccumGpuFrameSamples = 0;
        double perfAccumDrawCalls = 0.0;
        double perfAccumTriangles = 0.0;
        double perfAccumVisibleAnimatedUnits = 0.0;
        double perfAccumParticleCount = 0.0;
        int perfAccumFixedTicks = 0;
        int renderedFrames = 0;
        double elapsedSeconds = 0.0;
        const game::runtime::auto_quit::Policy autoQuit = game::runtime::auto_quit::fromEnvironment();
        if (autoQuit.enabled()) {
            std::cout << "[Run] Auto-quit policy enabled:";
            if (autoQuit.maxSeconds > 0.0) {
                std::cout << " seconds=" << autoQuit.maxSeconds;
            }
            if (autoQuit.maxFrames > 0) {
                std::cout << " frames=" << autoQuit.maxFrames;
            }
            std::cout << "\n";
        }

        while (running) {
            SDL_Event sdlEvent;

            while (SDL_PollEvent(&sdlEvent)) {
                if (sdlEvent.type == SDL_QUIT) {
                    running = false;
                    if (stopReason.empty()) stopReason = "SDL_QUIT event";
                }

                if (sdlEvent.type == SDL_WINDOWEVENT) {
                    if (sdlEvent.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                        sdlEvent.window.event == SDL_WINDOWEVENT_RESIZED) {
                        updateDrawableSizeAndViewport();
                        updateMouseScale();
                        updateCameraAspect();
                        if (renderer) {
                            renderer->onResize(drawableW, drawableH);
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

            const auto frameCpuStart = clock::now();
            const auto fixedStart = frameCpuStart;
            int fixedTicksThisFrame = 0;
            while (accumulator >= TIME_STEP) {
                game.fixedUpdate(TIME_STEP);
                accumulator -= TIME_STEP;
                ++fixedTicksThisFrame;
            }
            const auto fixedEnd = clock::now();

            const auto beginFrameStart = fixedEnd;
            if (renderer) {
                renderer->beginFrame(0.1f, 0.1f, 0.1f, 1.0f);
            }
            const auto renderBuildStart = clock::now();

            game.render(drawableW, drawableH);
            const auto renderBuildEnd = clock::now();
            const auto submitStart = renderBuildEnd;

            double presentWaitMs = 0.0;
            double gpuFrameMs = 0.0;
            bool gpuFrameValid = false;
            std::uint32_t drawCallsThisFrame = 0u;
            std::uint64_t trianglesThisFrame = 0u;
            std::uint32_t visibleAnimatedUnitsThisFrame = services.frameVisibleAnimatedUnits;
            std::uint32_t particleCountThisFrame = services.frameParticleCount;

            if (renderer) {
                renderer->endFrame();
                if (renderer->handlesPresentation()) {
                    IRenderBackend::BackendFrameTimings backendTimings;
                    if (renderer->getLastFrameTimings(backendTimings)) {
                        presentWaitMs = std::max(0.0, static_cast<double>(backendTimings.presentWaitMs));
                        if (backendTimings.gpuFrameValid) {
                            gpuFrameMs = std::max(0.0, static_cast<double>(backendTimings.gpuFrameMs));
                            gpuFrameValid = true;
                        }
                    }
                } else {
                    const auto presentStart = clock::now();
                    swapBuffers();
                    const auto presentEnd = clock::now();
                    presentWaitMs =
                        std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();
                }
            } else {
                const auto presentStart = clock::now();
                swapBuffers();
                const auto presentEnd = clock::now();
                presentWaitMs =
                    std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();
            }
            if (renderer) {
                IRenderBackend::BackendFrameStats backendStats;
                if (renderer->getLastFrameStats(backendStats)) {
                    drawCallsThisFrame = backendStats.drawCalls;
                    trianglesThisFrame = backendStats.triangles;
                }
            }
            const auto frameCpuEnd = clock::now();

            const double fixedMs = std::chrono::duration<double, std::milli>(fixedEnd - fixedStart).count();
            const double beginFrameMs =
                std::chrono::duration<double, std::milli>(renderBuildStart - beginFrameStart).count();
            const double renderBuildMs =
                std::chrono::duration<double, std::milli>(renderBuildEnd - renderBuildStart).count();
            const double submitRawMs =
                std::chrono::duration<double, std::milli>(frameCpuEnd - submitStart).count();
            const double submitMs = std::max(0.0, submitRawMs - presentWaitMs);
            const bool backendHandlesPresentation = renderer && renderer->handlesPresentation();
            const double totalPresentWaitMs =
                presentWaitMs + (backendHandlesPresentation ? beginFrameMs : 0.0);
            const double legacyRenderMs = beginFrameMs + renderBuildMs;
            const double legacySwapMs = std::max(0.0, submitRawMs);
            const double frameCpuMs = std::chrono::duration<double, std::milli>(frameCpuEnd - frameCpuStart).count();
            ++renderedFrames;
            elapsedSeconds += frameDt;

            frameCount++;
            fpsTimer += frameDt;
            perfAccumFrameMs += frameCpuMs;
            perfAccumFixedMs += fixedMs;
            perfAccumRenderBuildMs += renderBuildMs;
            perfAccumRenderSubmitMs += submitMs;
            perfAccumPresentWaitMs += totalPresentWaitMs;
            perfAccumLegacyRenderMs += legacyRenderMs;
            perfAccumLegacySwapMs += legacySwapMs;
            if (gpuFrameValid) {
                perfAccumGpuFrameMs += gpuFrameMs;
                ++perfAccumGpuFrameSamples;
            }
            perfAccumDrawCalls += static_cast<double>(drawCallsThisFrame);
            perfAccumTriangles += static_cast<double>(trianglesThisFrame);
            perfAccumVisibleAnimatedUnits += static_cast<double>(visibleAnimatedUnitsThisFrame);
            perfAccumParticleCount += static_cast<double>(particleCountThisFrame);
            perfAccumFixedTicks += fixedTicksThisFrame;
            if (fpsTimer >= 1.0) {
                const double frames = std::max(1, frameCount);
                const double fps = static_cast<double>(frameCount) / fpsTimer;
                const double avgFrameMs = perfAccumFrameMs / frames;
                const double avgFixedMs = perfAccumFixedMs / frames;
                const double avgRenderBuildMs = perfAccumRenderBuildMs / frames;
                const double avgRenderSubmitMs = perfAccumRenderSubmitMs / frames;
                const double avgPresentWaitMs = perfAccumPresentWaitMs / frames;
                const double avgLegacyRenderMs = perfAccumLegacyRenderMs / frames;
                const double avgLegacySwapMs = perfAccumLegacySwapMs / frames;
                const bool hasGpuFrameAverage = perfAccumGpuFrameSamples > 0;
                const double avgGpuFrameMs = hasGpuFrameAverage
                    ? (perfAccumGpuFrameMs / static_cast<double>(perfAccumGpuFrameSamples))
                    : 0.0;
                const std::uint32_t avgDrawCalls = static_cast<std::uint32_t>(
                    std::lround(perfAccumDrawCalls / frames));
                const std::uint64_t avgTriangles = static_cast<std::uint64_t>(
                    std::llround(perfAccumTriangles / frames));
                const std::uint32_t avgVisibleAnimatedUnits = static_cast<std::uint32_t>(
                    std::lround(perfAccumVisibleAnimatedUnits / frames));
                const std::uint32_t avgParticleCount = static_cast<std::uint32_t>(
                    std::lround(perfAccumParticleCount / frames));
                const int avgFixedTicks = static_cast<int>(std::lround(static_cast<double>(perfAccumFixedTicks) / frames));

                services.framePerf.fps = static_cast<float>(fps);
                services.framePerf.frameMs = static_cast<float>(avgFrameMs);
                services.framePerf.fixedMs = static_cast<float>(avgFixedMs);
                services.framePerf.renderBuildMs = static_cast<float>(avgRenderBuildMs);
                services.framePerf.renderSubmitMs = static_cast<float>(avgRenderSubmitMs);
                services.framePerf.presentWaitMs = static_cast<float>(avgPresentWaitMs);
                services.framePerf.gpuFrameMs = static_cast<float>(avgGpuFrameMs);
                services.framePerf.gpuFrameValid = hasGpuFrameAverage;
                services.framePerf.drawCalls = avgDrawCalls;
                services.framePerf.triangles = avgTriangles;
                services.framePerf.visibleAnimatedUnits = avgVisibleAnimatedUnits;
                services.framePerf.particleCount = avgParticleCount;
                services.framePerf.renderMs = static_cast<float>(avgLegacyRenderMs);
                services.framePerf.swapMs = static_cast<float>(avgLegacySwapMs);
                services.framePerf.fixedTicks = avgFixedTicks;

                std::cout << std::fixed << std::setprecision(1)
                          << "[Perf] FPS=" << fps
                          << " frame=" << avgFrameMs << "ms"
                          << " fixed=" << avgFixedMs << "ms"
                          << " build=" << avgRenderBuildMs << "ms"
                          << " submit=" << avgRenderSubmitMs << "ms"
                          << " present=" << avgPresentWaitMs << "ms"
                          << " gpu=" << (hasGpuFrameAverage ? avgGpuFrameMs : -1.0) << "ms"
                          << " draws=" << avgDrawCalls
                          << " tris=" << avgTriangles
                          << " units=" << avgVisibleAnimatedUnits
                          << " particles=" << avgParticleCount
                          << " render=" << avgLegacyRenderMs << "ms"
                          << " swap=" << avgLegacySwapMs << "ms"
                          << " ticks=" << avgFixedTicks << "\n";

                std::ostringstream perfJson;
                perfJson << std::fixed << std::setprecision(3)
                         << "[PerfJSON] {"
                         << "\"fps\":" << fps
                         << ",\"frame_cpu_ms\":" << avgFrameMs
                         << ",\"fixed_ms\":" << avgFixedMs
                         << ",\"render_build_ms\":" << avgRenderBuildMs
                         << ",\"render_submit_ms\":" << avgRenderSubmitMs
                         << ",\"present_wait_ms\":" << avgPresentWaitMs
                         << ",\"gpu_frame_ms\":" << (hasGpuFrameAverage ? avgGpuFrameMs : -1.0)
                         << ",\"gpu_frame_valid\":" << (hasGpuFrameAverage ? 1 : 0)
                         << ",\"draw_calls\":" << avgDrawCalls
                         << ",\"triangles\":" << avgTriangles
                         << ",\"visible_animated_units\":" << avgVisibleAnimatedUnits
                         << ",\"particle_count\":" << avgParticleCount
                         << ",\"legacy_render_ms\":" << avgLegacyRenderMs
                         << ",\"legacy_swap_ms\":" << avgLegacySwapMs
                         << ",\"fixed_ticks\":" << avgFixedTicks
                         << "}";
                std::cout << perfJson.str() << "\n";

                frameCount = 0;
                fpsTimer = 0.0;
                perfAccumFrameMs = 0.0;
                perfAccumFixedMs = 0.0;
                perfAccumRenderBuildMs = 0.0;
                perfAccumRenderSubmitMs = 0.0;
                perfAccumPresentWaitMs = 0.0;
                perfAccumLegacyRenderMs = 0.0;
                perfAccumLegacySwapMs = 0.0;
                perfAccumGpuFrameMs = 0.0;
                perfAccumGpuFrameSamples = 0;
                perfAccumDrawCalls = 0.0;
                perfAccumTriangles = 0.0;
                perfAccumVisibleAnimatedUnits = 0.0;
                perfAccumParticleCount = 0.0;
                perfAccumFixedTicks = 0;
            }

            if (autoQuit.enabled() &&
                game::runtime::auto_quit::shouldTrigger(autoQuit, elapsedSeconds, renderedFrames)) {
                running = false;
                if (stopReason.empty()) {
                    stopReason = "PAC_AUTO_QUIT policy reached";
                }
            }
        }

        if (stopReason.empty()) {
            stopReason = "main loop ended";
        }
        std::cout << "[Run] Exiting main loop: " << stopReason << "\n";
        game.shutdown();
        return 0;
    }
} // namespace

namespace game {

int runGame() {
    const std::string prefsPath = game::video::defaultPreferencesPath();
    int lastResult = 0;
    for (;;) {
        {
            game::video::Preferences startupPrefs = game::video::loadPreferences(prefsPath);
            if (startupPrefs.restartOnExit) {
                startupPrefs.restartOnExit = false;
                startupPrefs.bootMenuScreen.clear();
                std::string clearErr;
                if (!game::video::savePreferences(startupPrefs, prefsPath, &clearErr)) {
                    std::cerr << "[Video] Failed to clear stale restart request: " << clearErr << "\n";
                }
            }
        }

        GameRunner runner;
        if (!runner.init()) {
            runner.shutdown();
            return 1;
        }

        GameApp app;
        lastResult = runner.run(app);

        runner.shutdown();

        game::video::Preferences prefsAfterRun = game::video::loadPreferences(prefsPath);
        if (!prefsAfterRun.restartOnExit) {
            return lastResult;
        }

        prefsAfterRun.restartOnExit = false;
        std::string err;
        if (!game::video::savePreferences(prefsAfterRun, prefsPath, &err)) {
            std::cerr << "[Video] Failed to consume restart request: " << err << "\n";
            return lastResult;
        }

        std::cout << "[Run] Restart requested. Re-launching game session...\n";
    }
}

} // namespace game

