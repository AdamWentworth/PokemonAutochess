// src/game/GameRunner.cpp

#include "game/runtime/GameRunner.h"

#include "game/runtime/GameApp.h"

#include "engine/core/EngineServices.h"
#include "engine/runtime/FixedStep.h"
#include "engine/core/GameContext.h"
#include "engine/core/GameLoop.h"
#include "engine/events/EventBus.h"
#include "engine/input/InputEvent.h"
#include "engine/platform/Window.h"
#include "engine/render/Camera3D.h"
#include "engine/render/IRenderBackend.h"
#include "engine/ui/BootLoadingView.h"
#include "engine/utils/ResourceManager.h"
#include "engine/utils/ShaderCache.h"
#include "game/runtime/AutoQuitPolicy.h"
#include "game/runtime/renderer/RendererBackendBootstrap.h"
#include "game/runtime/RuntimeBootLoading.h"
#include "game/runtime/loop/RuntimeFixedStepPhase.h"
#include "game/runtime/loop/RuntimeFrameObservation.h"
#include "game/runtime/loop/RuntimeFramePerfCapture.h"
#include "game/runtime/loop/RuntimeLoopConfig.h"
#include "game/runtime/loop/RuntimeLoopControl.h"
#include "game/runtime/RuntimeOpenGlBootstrap.h"
#include "game/runtime/loop/RuntimePerfAccumulator.h"
#include "game/runtime/loop/RuntimePerfLogging.h"
#include "game/runtime/RuntimeRelaunchLoop.h"
#include "game/runtime/renderer/RuntimeRendererActivation.h"
#include "game/runtime/renderer/RuntimeRendererRecovery.h"
#include "game/runtime/renderer/RuntimeRendererStartupState.h"
#include "game/runtime/video/RuntimeSdlEventDispatch.h"
#include "game/runtime/video/RuntimeSdlInput.h"
#include "game/runtime/video/RuntimeSdlVideoMode.h"
#include "game/runtime/startup/RuntimeStartupConfig.h"
#include "game/runtime/startup/RuntimeStartupPresentation.h"
#include "game/runtime/startup/RuntimeStartupSession.h"
#include "game/runtime/startup/RuntimeStartupVideoOverride.h"
#include "game/runtime/video/RuntimeWindowBootstrap.h"
#include "game/runtime/video/VideoInitGuards.h"
#include "game/runtime/video/VideoPreferences.h"

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
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {
    constexpr unsigned int START_W  = 1280;
    constexpr unsigned int START_H  = 720;

    const char* glStringOrUnknown(GLenum token) {
        const GLubyte* s = glGetString(token);
        return s ? reinterpret_cast<const char*>(s) : "<unknown>";
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
        void syncVideoModeState();
        void noteCurrentWindowModeChanged(bool saveImmediately);
        void saveVideoModePreferences();
        game::runtime::video_mode::RequestedVideoMode resolveRequestedVideoMode(
            int width,
            int height,
            bool fullscreenWanted) const;
        bool applyVideoModeInternal(int width, int height, bool fullscreenWanted, bool persistChange);
        bool applyVideoMode(int width, int height, bool fullscreenWanted);
        GameContext::VideoMode queryVideoMode() const;
        void syncLivePresentationSettings();
        void enforceFrameCap(const std::chrono::high_resolution_clock::time_point& frameStart);

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
        std::string prefsPath;

        bool initialized = false;
        game::video::RendererBackend requestedBackend = game::video::RendererBackend::Auto;
        game::video::RendererBackend activeBackend = game::video::RendererBackend::OpenGL;

        int drawableW = (int)START_W;
        int drawableH = (int)START_H;

        int windowW = (int)START_W;
        int windowH = (int)START_H;
        int defaultWindowedW = (int)START_W;
        int defaultWindowedH = (int)START_H;
        int lastWindowedW = (int)START_W;
        int lastWindowedH = (int)START_H;
        bool fullscreen = false;
        bool lastWindowedMaximized = false;
        bool videoModePreferencesDirty = false;
        bool windowHasOpenGLContext = false;
        bool glFunctionsReady = false;
        bool appliedVsyncEnabled = false;

        float mouseScaleX = 1.0f;
        float mouseScaleY = 1.0f;
    };

    bool GameRunner::init() {
        prefsPath = game::video::defaultPreferencesPath();
        const auto startupSession =
            game::runtime::startup_session::prepareFromEnvironment(prefsPath, std::cout, std::cerr);
        requestedBackend = startupSession.requestedBackend;
        activeBackend = startupSession.activeBackend;
        game::runtime::startup_session::applyToServices(startupSession, services);
        services.videoPreferencesPath = prefsPath;
        appliedVsyncEnabled = services.vsyncEnabled;
        const game::video::Preferences startupVideoPrefs = game::video::loadPreferences(prefsPath);

        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::cerr << "[GameRunner] SDL_Init failed: " << SDL_GetError() << "\n";
            return false;
        }

        const auto startupDisplayBounds =
            game::runtime::video_mode::queryPrimaryDisplayUsableBounds(std::cerr);
        const auto startupPlacement =
            game::runtime::video_mode::resolveStartupWindowPlacement(
                startupVideoPrefs,
                startupDisplayBounds);
        const bool hasSavedWindowedSize =
            startupVideoPrefs.windowedWidth > 0 && startupVideoPrefs.windowedHeight > 0;
        defaultWindowedW = startupPlacement.width;
        defaultWindowedH = startupPlacement.height;
        lastWindowedW = startupPlacement.width;
        lastWindowedH = startupPlacement.height;
        lastWindowedMaximized = startupPlacement.maximized;

        const auto initialWindow = game::runtime::window_bootstrap::openWindow(
            game::runtime::window_bootstrap::OpenRequest{
                game::runtime::backend_bootstrap::graphicsApiForBackend(activeBackend),
                services.vsyncEnabled},
            [this](Window::GraphicsApi graphicsApi, bool vsyncEnabled) {
                window = std::make_unique<Window>(
                    "Pokemon Autochess",
                    defaultWindowedW,
                    defaultWindowedH,
                    graphicsApi,
                    vsyncEnabled);
            },
            [this]() {
                return window && window->hasOpenGLContext();
            });
        if (!initialWindow.success) {
            std::cerr << "[GameRunner] Window init failed: " << initialWindow.error << "\n";
            return false;
        }
        windowHasOpenGLContext = initialWindow.hasOpenGlContext;
        glFunctionsReady = initialWindow.glFunctionsReady;

        if (window && window->getSDLWindow()) {
            if (startupDisplayBounds.valid &&
                (!hasSavedWindowedSize || startupPlacement.maximized)) {
                SDL_SetWindowPosition(
                    window->getSDLWindow(),
                    startupPlacement.x,
                    startupPlacement.y);
            }
            if (!startupVideoPrefs.fullscreen && startupPlacement.maximized) {
                SDL_MaximizeWindow(window->getSDLWindow());
            }
        }
        syncVideoModeState();
        if (startupVideoPrefs.fullscreen &&
            !applyVideoModeInternal(0, 0, true, false)) {
            std::cerr << "[Video] Failed to apply saved startup fullscreen mode.\n";
        }

        const auto startupOverrideResult = game::runtime::startup_video_override::apply(
            game::runtime::startup_config::readStartupVideoOverride(std::cerr),
            [this]() { return this->queryVideoMode(); },
            [this](int width, int height, bool isFullscreen) {
                return this->applyVideoModeInternal(width, height, isFullscreen, false);
            });
        if (startupOverrideResult.attempted) {
            (startupOverrideResult.applied ? std::cout : std::cerr) << startupOverrideResult.message << "\n";
        }

        const auto preloadBootstrap = game::runtime::opengl_bootstrap::bootstrapLoadingPresentation(
            windowHasOpenGLContext,
            game::runtime::opengl_bootstrap::PreloadCallbacks{
                [this](std::string* outError) {
                    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
                        if (outError) *outError = "gladLoadGLLoader failed";
                        return false;
                    }
                    return true;
                },
                [this]() {
                    bootLoadingView = std::make_unique<BootLoadingView>();
                    bootLoadingView->init(shaderCache);
                },
                [this](std::string_view title) {
                    this->setTitle(std::string(title));
                },
                [this](float r, float g, float b, float a) {
                    glClearColor(r, g, b, a);
                    glClear(GL_COLOR_BUFFER_BIT);
                    swapBuffers();
                },
                [this]() { return this->pumpPreloadEvents(); }});
        if (!preloadBootstrap.success) {
            std::cerr << "[GameRunner] Failed to initialize GLAD";
            if (!preloadBootstrap.error.empty()) {
                std::cerr << ": " << preloadBootstrap.error;
            }
            std::cerr << "\n";
            return false;
        }
        glFunctionsReady = preloadBootstrap.glFunctionsReady;

        auto rendererResult = game::runtime::renderer_recovery::createWithOpenGlFallback(
            game::runtime::renderer_recovery::Inputs{activeBackend, services.activeRendererBackend},
            [this](game::video::RendererBackend backend, std::string* outError) {
                return game::runtime::backend_bootstrap::createRenderBackend(
                    backend,
                    window ? window->getSDLWindow() : nullptr,
                    drawableW,
                    drawableH,
                    services.vsyncEnabled,
                    services.preferredGpuAdapter,
                    outError);
            },
            [this]() {
                window.reset();
                const auto fallbackWindow = game::runtime::window_bootstrap::openWindow(
                    game::runtime::window_bootstrap::OpenRequest{
                        Window::GraphicsApi::OpenGL,
                        services.vsyncEnabled},
                    [this](Window::GraphicsApi graphicsApi, bool vsyncEnabled) {
                        window = std::make_unique<Window>(
                            "Pokemon Autochess",
                            static_cast<int>(START_W),
                            static_cast<int>(START_H),
                            graphicsApi,
                            vsyncEnabled);
                    },
                    [this]() {
                        return window && window->hasOpenGLContext();
                    });
                windowHasOpenGLContext = fallbackWindow.hasOpenGlContext;
                glFunctionsReady = fallbackWindow.glFunctionsReady;
                return game::runtime::renderer_recovery::OpenGlWindowResult{
                    fallbackWindow.success,
                    fallbackWindow.error};
            },
            [this](std::string* outError) {
                const bool ok = game::runtime::opengl_bootstrap::initializeOpenGlFunctions(
                    [this](std::string* loaderError) {
                        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
                            if (loaderError) *loaderError = "gladLoadGLLoader failed";
                            return false;
                        }
                        return true;
                    },
                    outError);
                glFunctionsReady = ok;
                return ok;
            },
            [this]() {
                updateDrawableSizeAndViewport();
                updateMouseScale();
            });
        if (rendererResult.rendererBackendFallback) {
            services.rendererBackendFallback = true;
            services.rendererBackendFallbackReason = rendererResult.rendererBackendFallbackReason;
            std::cout << "[Renderer] " << services.rendererBackendFallbackReason << "\n";
        }
        activeBackend = rendererResult.activeBackend;
        services.activeRendererBackend = rendererResult.activeBackendName;
        renderer = std::move(rendererResult.renderer);
        if (!renderer) {
            switch (rendererResult.failureStage) {
            case game::runtime::renderer_recovery::FailureStage::FallbackWindowOpen:
                std::cerr << "[Renderer] OpenGL fallback window init failed: "
                          << rendererResult.error << "\n";
                return false;
            case game::runtime::renderer_recovery::FailureStage::FallbackOpenGlInit:
                std::cerr << "[GameRunner] Failed to initialize GLAD after fallback";
                if (!rendererResult.error.empty()) {
                    std::cerr << ": " << rendererResult.error;
                }
                std::cerr << "\n";
                return false;
            case game::runtime::renderer_recovery::FailureStage::InitialBackendCreate:
            case game::runtime::renderer_recovery::FailureStage::FallbackBackendCreate:
                std::cerr << "[Renderer] Failed to create backend '" << services.activeRendererBackend
                          << "' (" << rendererResult.error << ").\n";
                return false;
            case game::runtime::renderer_recovery::FailureStage::None:
            default:
                std::cerr << "[Renderer] Failed to create backend '" << services.activeRendererBackend
                          << "'.\n";
                return false;
            }
        }

        game::runtime::renderer_startup_state::OpenGlStrings openGlStrings;
        if (renderer->requiresOpenGLContext()) {
            openGlStrings.vendor = glStringOrUnknown(GL_VENDOR);
            openGlStrings.renderer = glStringOrUnknown(GL_RENDERER);
            openGlStrings.version = glStringOrUnknown(GL_VERSION);
            openGlStrings.glslVersion = glStringOrUnknown(GL_SHADING_LANGUAGE_VERSION);
        }
        const auto activationInputs =
            game::runtime::renderer_startup_state::makeActivationInputs(
                services,
                *renderer,
                openGlStrings);
        const auto activation =
            game::runtime::renderer_startup_state::applyAndLog(services, activationInputs, std::cout);

        if (!activation.discreteRequirementSatisfied) {
            std::cerr << "[GPU] Discrete GPU required by settings, but integrated GPU is active.\n";
            std::cerr << "[GPU] Change Graphics preference to high performance or choose a discrete adapter.\n";
            return false;
        }

        const auto fontInit = game::runtime::startup_presentation::initializeFonts(
            []() { return TTF_Init(); },
            []() { return std::string(TTF_GetError()); });
        if (!fontInit.succeeded) {
            std::cerr << "[GameRunner] TTF_Init error: " << fontInit.error << "\n";
        }

        camera = game::runtime::startup_presentation::createDefaultCamera(drawableW, drawableH);
        game::runtime::startup_presentation::primeInitialLoadingFrame(
            renderer.get(),
            drawableW,
            drawableH,
            [this](float progress01) {
                // Ensure native backends show the same dark loading frame immediately,
                // avoiding a temporary OS white window before preload UI starts updating.
                renderBootLoading(progress01);
            });

        initialized = true;
        std::cout << "[Init] Game runner initialized.\n";
        return true;
    }

    void GameRunner::shutdown() {
        std::cout << "[Shutdown] Game runner...\n";

        if (videoModePreferencesDirty) {
            saveVideoModePreferences();
        }

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

    void GameRunner::syncVideoModeState() {
        updateDrawableSizeAndViewport();
        updateMouseScale();
        updateCameraAspect();
        if (window && window->getSDLWindow()) {
            const Uint32 flags = SDL_GetWindowFlags(window->getSDLWindow());
            fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0 ||
                         (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
            if (!fullscreen) {
                lastWindowedW = std::max(640, windowW);
                lastWindowedH = std::max(360, windowH);
                lastWindowedMaximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
            }
        }
        if (renderer) {
            renderer->onResize(drawableW, drawableH);
        }
    }

    void GameRunner::noteCurrentWindowModeChanged(bool saveImmediately) {
        videoModePreferencesDirty = true;
        if (saveImmediately) {
            saveVideoModePreferences();
        }
    }

    void GameRunner::saveVideoModePreferences() {
        if (prefsPath.empty()) return;

        game::video::Preferences prefs = game::video::loadPreferences(prefsPath);
        const int safeWindowedW = std::max(640, lastWindowedW);
        const int safeWindowedH = std::max(360, lastWindowedH);
        if (prefs.fullscreen == fullscreen &&
            prefs.windowedWidth == safeWindowedW &&
            prefs.windowedHeight == safeWindowedH &&
            prefs.windowedMaximized == lastWindowedMaximized) {
            videoModePreferencesDirty = false;
            return;
        }

        prefs.fullscreen = fullscreen;
        prefs.windowedWidth = safeWindowedW;
        prefs.windowedHeight = safeWindowedH;
        prefs.windowedMaximized = lastWindowedMaximized;

        std::string saveErr;
        if (!game::video::savePreferences(prefs, prefsPath, &saveErr)) {
            std::cerr << "[Video] Failed to save video mode preferences: " << saveErr << "\n";
            return;
        }
        videoModePreferencesDirty = false;
    }

    game::runtime::video_mode::RequestedVideoMode GameRunner::resolveRequestedVideoMode(
        int width,
        int height,
        bool fullscreenWanted) const {
        int targetWidth = width;
        int targetHeight = height;

        if (fullscreenWanted) {
            if (targetWidth <= 0 || targetHeight <= 0) {
                SDL_DisplayMode desktopMode{};
                int displayIndex = 0;
                if (window && window->getSDLWindow()) {
                    const int queriedDisplayIndex = SDL_GetWindowDisplayIndex(window->getSDLWindow());
                    if (queriedDisplayIndex >= 0) {
                        displayIndex = queriedDisplayIndex;
                    }
                }
                if (SDL_GetDesktopDisplayMode(displayIndex, &desktopMode) == 0 &&
                    desktopMode.w > 0 &&
                    desktopMode.h > 0) {
                    targetWidth = desktopMode.w;
                    targetHeight = desktopMode.h;
                } else {
                    targetWidth = std::max(640, drawableW);
                    targetHeight = std::max(360, drawableH);
                }
            }
        } else if (targetWidth <= 0 || targetHeight <= 0) {
            targetWidth = std::max(640, lastWindowedW > 0 ? lastWindowedW : defaultWindowedW);
            targetHeight = std::max(360, lastWindowedH > 0 ? lastWindowedH : defaultWindowedH);
        }

        return game::runtime::video_mode::sanitizeRequestedVideoMode(
            targetWidth,
            targetHeight,
            fullscreenWanted);
    }

    bool GameRunner::applyVideoModeInternal(int width,
                                            int height,
                                            bool fullscreenWanted,
                                            bool persistChange) {
        if (!window || !window->getSDLWindow()) return false;
        const auto requested = resolveRequestedVideoMode(width, height, fullscreenWanted);
        const auto result = game::runtime::video_mode::applyRequestedVideoMode(
            window->getSDLWindow(),
            requested,
            std::cerr);
        if (!result.success) {
            return false;
        }
        syncVideoModeState();
        fullscreen = result.fullscreen;
        if (persistChange) {
            noteCurrentWindowModeChanged(true);
        }
        return true;
    }

    bool GameRunner::applyVideoMode(int width, int height, bool fullscreenWanted) {
        return applyVideoModeInternal(width, height, fullscreenWanted, true);
    }

    GameContext::VideoMode GameRunner::queryVideoMode() const {
        const int currentWidth = fullscreen ? drawableW : windowW;
        const int currentHeight = fullscreen ? drawableH : windowH;
        return game::runtime::video_mode::makeCurrentVideoMode(
            currentWidth,
            currentHeight,
            fullscreen);
    }

    void GameRunner::syncLivePresentationSettings() {
        if (appliedVsyncEnabled == services.vsyncEnabled) {
            return;
        }

        bool applied = false;
        if (windowHasOpenGLContext && window) {
            applied = window->setVSyncEnabled(services.vsyncEnabled);
        } else if (renderer && renderer->handlesPresentation()) {
            renderer->setVSyncEnabled(services.vsyncEnabled);
            applied = true;
        }

        if (applied) {
            appliedVsyncEnabled = services.vsyncEnabled;
            std::cout << "[Video] VSync live set: "
                      << (appliedVsyncEnabled ? "On" : "Off") << "\n";
        } else {
            std::cerr << "[Video] Failed to apply live VSync toggle.\n";
        }
    }

    void GameRunner::enforceFrameCap(const std::chrono::high_resolution_clock::time_point& frameStart) {
        const int fpsCap = game::video::sanitizeFpsCap(services.fpsCap);
        if (fpsCap <= 0) return;

        using clock = std::chrono::high_resolution_clock;
        const auto frameBudget =
            std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(fpsCap)));
        const auto deadline = frameStart + frameBudget;

        auto now = clock::now();
        if (now >= deadline) return;

        const auto coarseSleepThreshold = std::chrono::milliseconds(2);
        const auto coarseSleepPadding = std::chrono::milliseconds(1);
        if (deadline - now > coarseSleepThreshold) {
            std::this_thread::sleep_until(deadline - coarseSleepPadding);
        }

        while ((now = clock::now()) < deadline) {
            std::this_thread::yield();
        }
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
            if (game::runtime::boot_loading::shouldAbortPreloadEvent(e)) return false;

            if (game::runtime::sdl_input::isResizeWindowEvent(e)) {
                syncVideoModeState();
                noteCurrentWindowModeChanged(false);
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
            renderer->beginFrame(0.05f, 0.05f, 0.07f, 1.0f);
            std::array<IRenderBackend::DebugQuad, game::runtime::boot_loading::kFallbackLoadingQuadCount> quads{};
            if (game::runtime::boot_loading::buildFallbackLoadingQuads(
                    drawableW,
                    drawableH,
                    progress01,
                    quads)) {
                renderer->drawDebugQuads(
                    quads.data(),
                    static_cast<int>(quads.size()),
                    drawableW,
                    drawableH);
            }
            renderer->endFrame();
        }
    }

    int GameRunner::run(GameLoop& game) {
        if (!initialized) return 1;

        std::cout << "[Run] Main loop @ "
                  << static_cast<int>(engine::runtime::fixed_step::kHz)
                  << " Hz...\n";
        static const int maxFixedTicksPerFrame =
            game::runtime::loop_config::readMaxFixedTicksPerFrameFromEnvironment(std::cerr);
        std::cout << "[Run] Fixed tick budget: " << maxFixedTicksPerFrame << " ticks/frame\n";

        game::runtime::loop_control::State loopState;
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
        ctx.requestQuit = [&loopState]() {
            game::runtime::loop_control::requestStop(loopState, "requestQuit() callback invoked");
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

        if (!game::runtime::loop_control::isRunning(loopState)) {
            game.shutdown();
            return 0;
        }

        using clock = std::chrono::high_resolution_clock;
        auto previous = clock::now();
        double accumulator = 0.0;

        game::runtime::perf_accum::RollingAccumulator perfAccumulator;
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

        while (game::runtime::loop_control::isRunning(loopState)) {
            syncLivePresentationSettings();
            SDL_Event sdlEvent;

            while (SDL_PollEvent(&sdlEvent)) {
                game::runtime::sdl_event_dispatch::Callbacks eventCallbacks;
                eventCallbacks.onResize = [this, &ctx]() {
                    syncVideoModeState();
                    noteCurrentWindowModeChanged(false);
                    ctx.drawableW = drawableW;
                    ctx.drawableH = drawableH;
                };
                eventCallbacks.onInputEvent = [&game](const InputEvent& event) {
                    game.handleEvent(event);
                };
                eventCallbacks.makeTranslationContext = [this]() {
                    game::runtime::sdl_input::TranslationContext inputContext;
                    inputContext.mouseScaleX = mouseScaleX;
                    inputContext.mouseScaleY = mouseScaleY;
                    inputContext.windowW = windowW;
                    inputContext.windowH = windowH;
                    inputContext.drawableW = drawableW;
                    inputContext.drawableH = drawableH;
                    return inputContext;
                };
                game::runtime::sdl_event_dispatch::dispatch(sdlEvent, loopState, eventCallbacks);
            }

            auto now = clock::now();
            double frameDt = std::chrono::duration<double>(now - previous).count();
            frameDt = game::runtime::loop_config::clampFrameDeltaSeconds(frameDt);
            previous = now;
            const auto frameStart = now;

            accumulator += frameDt;

            const auto frameCpuStart = clock::now();
            const auto fixedPhase = game::runtime::fixed_step_phase::execute(
                accumulator,
                engine::runtime::fixed_step::kSeconds,
                maxFixedTicksPerFrame,
                services,
                [&game](float dt) { game.fixedUpdate(dt); });
            accumulator = fixedPhase.accumulator;
            const auto beginFrameStart = clock::now();
            if (renderer) {
                renderer->beginFrame(0.1f, 0.1f, 0.1f, 1.0f);
            }
            const auto renderBuildStart = clock::now();

            game.render(drawableW, drawableH);
            const auto renderBuildEnd = clock::now();
            const auto submitStart = renderBuildEnd;
            const auto serviceSnapshot = game::runtime::frame_observation::captureServiceSnapshot(services);

            game::runtime::frame_perf_capture::BackendFrameInputs backendPerfInputs;
            if (renderer) {
                renderer->endFrame();
                IRenderBackend::BackendFrameTimings backendTimings;
                backendPerfInputs.rendererHandlesPresentation = renderer->handlesPresentation();
                backendPerfInputs.hasBackendTimings = renderer->getLastFrameTimings(backendTimings);
                backendPerfInputs.backendTimings = backendTimings;
                if (renderer->handlesPresentation()) {
                } else {
                    const auto presentStart = clock::now();
                    swapBuffers();
                    const auto presentEnd = clock::now();
                    backendPerfInputs.measuredPresentWaitMs =
                        std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();
                }
                IRenderBackend::BackendFrameStats backendStats;
                backendPerfInputs.hasBackendStats = renderer->getLastFrameStats(backendStats);
                backendPerfInputs.backendStats = backendStats;
            } else {
                const auto presentStart = clock::now();
                swapBuffers();
                const auto presentEnd = clock::now();
                backendPerfInputs.measuredPresentWaitMs =
                    std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();
            }
            const auto backendPerf =
                game::runtime::frame_perf_capture::resolveBackendFrameOutputs(backendPerfInputs);
            const auto frameCpuEnd = clock::now();

            const double beginFrameMs =
                std::chrono::duration<double, std::milli>(renderBuildStart - beginFrameStart).count();
            const double renderBuildMs = std::chrono::duration<double, std::milli>(
                                             renderBuildEnd - renderBuildStart)
                                             .count();
            const double submitRawMs =
                std::chrono::duration<double, std::milli>(frameCpuEnd - submitStart).count();
            const double submitMs =
                game::runtime::frame_perf_capture::computeSubmitMs(
                    submitRawMs,
                    backendPerf.presentWaitMs);
            const double totalPresentWaitMs =
                game::runtime::frame_perf_capture::computeTotalPresentWaitMs(
                    backendPerfInputs.rendererHandlesPresentation,
                    beginFrameMs,
                    backendPerf.presentWaitMs);
            const double legacyRenderMs = beginFrameMs + renderBuildMs;
            const double legacySwapMs = std::max(0.0, submitRawMs);
            const double frameCpuMs = std::chrono::duration<double, std::milli>(frameCpuEnd - frameCpuStart).count();
            game::runtime::loop_control::notePresentedFrame(loopState, frameDt);

            game::runtime::frame_observation::SampleInputs sampleInputs;
            sampleInputs.frameDt = frameDt;
            sampleInputs.frameCpuMs = frameCpuMs;
            sampleInputs.fixedMs = fixedPhase.fixedMs;
            sampleInputs.fixedTickWorkMs = fixedPhase.fixedTickWorkMs;
            sampleInputs.renderBuildMs = renderBuildMs;
            sampleInputs.renderSubmitMs = submitMs;
            sampleInputs.presentWaitMs = totalPresentWaitMs;
            sampleInputs.legacyRenderMs = legacyRenderMs;
            sampleInputs.legacySwapMs = legacySwapMs;
            sampleInputs.gpuFrameMs = backendPerf.gpuFrameMs;
            sampleInputs.gpuFrameValid = backendPerf.gpuFrameValid;
            sampleInputs.drawCalls = backendPerf.drawCalls;
            sampleInputs.triangles = backendPerf.triangles;
            sampleInputs.indexedOpaqueDraws = backendPerf.indexedOpaqueDraws;
            sampleInputs.indexedBlendDraws = backendPerf.indexedBlendDraws;
            sampleInputs.indexedCachedDraws = backendPerf.indexedCachedDraws;
            sampleInputs.indexedDynamicDraws = backendPerf.indexedDynamicDraws;
            sampleInputs.indexedInstancedDraws = backendPerf.indexedInstancedDraws;
            sampleInputs.indexedOutlineBatches = backendPerf.indexedOutlineBatches;
            sampleInputs.indexedGeometrySwitches = backendPerf.indexedGeometrySwitches;
            sampleInputs.indexedMaterialSwitches = backendPerf.indexedMaterialSwitches;
            sampleInputs.indexedTextureSwitches = backendPerf.indexedTextureSwitches;
            sampleInputs.indexedGlTextureBindCalls = backendPerf.indexedGlTextureBindCalls;
            sampleInputs.indexedD3d12PsoSets = backendPerf.indexedD3d12PsoSets;
            sampleInputs.indexedD3d12DescriptorTableSets =
                backendPerf.indexedD3d12DescriptorTableSets;
            sampleInputs.fastSceneInstances = backendPerf.fastSceneInstances;
            sampleInputs.fastSceneDrawClasses = backendPerf.fastSceneDrawClasses;
            sampleInputs.fastSceneVisibleSkeletons =
                backendPerf.fastSceneVisibleSkeletons;
            sampleInputs.fastScenePaletteUploadBytes =
                backendPerf.fastScenePaletteUploadBytes;
            sampleInputs.fastSceneMaterialTableBinds =
                backendPerf.fastSceneMaterialTableBinds;
            sampleInputs.fastSceneIndirectCommands =
                backendPerf.fastSceneIndirectCommands;
            sampleInputs.fixedBreakdown = fixedPhase.fixedBreakdown;
            sampleInputs.fixedTicks = fixedPhase.fixedTicks;
            sampleInputs.fixedTicksDropped = fixedPhase.fixedTicksDropped;
            perfAccumulator.addFrame(
                game::runtime::frame_observation::makePerfSample(sampleInputs, serviceSnapshot));
            if (perfAccumulator.readyToEmit()) {
                const auto perfSummary = perfAccumulator.makeSummaryAndReset();
                services.framePerf = perfSummary.framePerf;
                std::cout << game::runtime::perf_logging::formatPerfLine(services.framePerf) << "\n";
                std::cout << game::runtime::perf_logging::formatPerfJson(services.framePerf) << "\n";
            }

            if (autoQuit.enabled()) {
                game::runtime::loop_control::applyAutoQuit(autoQuit, loopState);
            }

            if (game::runtime::loop_control::isRunning(loopState)) {
                enforceFrameCap(frameStart);
            }
        }

        std::cout << "[Run] Exiting main loop: "
                  << game::runtime::loop_control::effectiveStopReason(loopState) << "\n";
        game.shutdown();
        return 0;
    }
} // namespace

namespace game {

int runGame() {
    const std::string prefsPath = game::video::defaultPreferencesPath();
    return game::runtime::relaunch_loop::runWithRestartPolicy(
        prefsPath,
        []() {
        GameRunner runner;
        if (!runner.init()) {
            runner.shutdown();
            return 1;
        }

        GameApp app;
            const int lastResult = runner.run(app);

        runner.shutdown();
            return lastResult;
        },
        std::cout,
        std::cerr);
}

} // namespace game


