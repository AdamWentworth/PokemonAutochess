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
#include "engine/platform/Window.h"
#include "engine/render/Camera3D.h"
#include "engine/render/IRenderBackend.h"
#include "engine/ui/BootLoadingView.h"
#include "engine/utils/ResourceManager.h"
#include "engine/utils/ShaderCache.h"
#include "game/runtime/GpuAdapters.h"
#include "game/runtime/AutoQuitPolicy.h"
#include "game/runtime/RendererBackendBootstrap.h"
#include "game/runtime/RuntimeBootLoading.h"
#include "game/runtime/RuntimeLoopConfig.h"
#include "game/runtime/RuntimeRestartPolicy.h"
#include "game/runtime/RuntimeSdlInput.h"
#include "game/runtime/RuntimeSdlVideoMode.h"
#include "game/runtime/RendererStartupDiagnostics.h"
#include "game/runtime/RuntimeStartupConfig.h"
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
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace {
    constexpr unsigned int START_W  = 1280;
    constexpr unsigned int START_H  = 720;
    constexpr float TIME_STEP = 1.0f / 60.0f;

    bool looksIntegratedGpu(const std::string& vendor, const std::string& renderer) {
        // Heuristic: Intel OpenGL contexts on hybrid laptops are typically iGPU.
        return game::runtime::startup_diag::activeRendererMatchesPreferredAdapter(vendor, "intel") ||
            game::runtime::startup_diag::activeRendererMatchesPreferredAdapter(renderer, "intel");
    }

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

        services.bootMenuScreen = game::runtime::startup_config::consumeBootMenuScreen(prefs);
        if (!services.bootMenuScreen.empty()) {
            std::string consumeErr;
            if (!game::video::savePreferences(prefs, prefsPath, &consumeErr)) {
                std::cerr << "[Video] Failed to clear one-shot boot menu screen: " << consumeErr << "\n";
            }
        }

        const auto resolvedRendererPref = game::runtime::startup_config::resolveRendererPreference(
            prefs,
            engine::env::get("PAC_RENDER_BACKEND"));
        if (resolvedRendererPref.overriddenByEnv) {
            std::cout << "[Renderer] PAC_RENDER_BACKEND override: " << resolvedRendererPref.backendToken << "\n";
            std::cout << "[Renderer] Note: env override is active; saved Display settings "
                         "(Render API) are ignored until PAC_RENDER_BACKEND is unset.\n";
        }
        requestedBackend = resolvedRendererPref.requestedBackend;
        services.requestedRendererBackend = resolvedRendererPref.requestedBackendName;
        services.vsyncEnabled = prefs.vsync;
        services.requireDiscreteGpu = prefs.requireDiscreteGpu;
        services.preferredGpuAdapter = prefs.preferredGpuAdapter;
        services.characterInkingEnabled = prefs.characterInking;

        {
            const auto adapters = game::video::enumerateSystemGpuAdapters();
            services.availableGpuAdapters =
                game::runtime::startup_diag::collectGpuAdapterNames(adapters);
            game::runtime::startup_diag::logGpuAdapterInventory(
                adapters,
                services.preferredGpuAdapter,
                std::cout);
        }

        {
            const auto backendSelection = game::runtime::backend_bootstrap::selectStartupBackend(
                requestedBackend,
                services.requestedRendererBackend);
            activeBackend = backendSelection.activeBackend;
            services.rendererBackendFallback = backendSelection.fallback;
            services.rendererBackendFallbackReason = backendSelection.fallbackReason;
        }
        if (services.rendererBackendFallback) {
            std::cout << "[Renderer] " << services.rendererBackendFallbackReason << "\n";
        }
        services.activeRendererBackend = game::video::rendererBackendName(activeBackend);

        try {
            window = std::make_unique<Window>(
                "Pokemon Autochess",
                static_cast<int>(START_W),
                static_cast<int>(START_H),
                game::runtime::backend_bootstrap::graphicsApiForBackend(activeBackend),
                services.vsyncEnabled);
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

        const auto startupVideoOverride =
            game::runtime::startup_config::readStartupVideoOverride(std::cerr);
        if (startupVideoOverride.enabled()) {
            const auto overrideMode = game::runtime::startup_config::resolveStartupVideoMode(
                startupVideoOverride,
                windowW,
                windowH,
                fullscreen);
            if (applyVideoMode(overrideMode.width, overrideMode.height, overrideMode.fullscreen)) {
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
        renderer = game::runtime::backend_bootstrap::createRenderBackend(activeBackend,
                                                                         window ? window->getSDLWindow() : nullptr,
                                                                         drawableW,
                                                                         drawableH,
                                                                         services.vsyncEnabled,
                                                                         services.preferredGpuAdapter,
                                                                         &backendCreateError);
        if (!renderer) {
            if (activeBackend != game::video::RendererBackend::OpenGL) {
                services.rendererBackendFallback = true;
                services.rendererBackendFallbackReason =
                    game::runtime::backend_bootstrap::makeBackendInitFallbackReason(
                        services.activeRendererBackend,
                        backendCreateError);
                std::cout << "[Renderer] " << services.rendererBackendFallbackReason << "\n";

                window.reset();
                try {
                    activeBackend = game::video::RendererBackend::OpenGL;
                    services.activeRendererBackend = game::video::rendererBackendName(activeBackend);
                    window = std::make_unique<Window>(
                        "Pokemon Autochess",
                        static_cast<int>(START_W),
                        static_cast<int>(START_H),
                        Window::GraphicsApi::OpenGL,
                        services.vsyncEnabled);
                    windowHasOpenGLContext = true;
                    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
                        std::cerr << "[GameRunner] Failed to initialize GLAD after fallback\n";
                        return false;
                    }
                    glFunctionsReady = true;
                    updateDrawableSizeAndViewport();
                    updateMouseScale();
                    renderer = game::runtime::backend_bootstrap::createRenderBackend(
                        activeBackend,
                        window ? window->getSDLWindow() : nullptr,
                        drawableW,
                        drawableH,
                        services.vsyncEnabled,
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

        game::runtime::startup_diag::ActiveRendererSummary startupSummary;
        startupSummary.requestedBackend = services.requestedRendererBackend;
        startupSummary.activeBackend = services.activeRendererBackend;
        startupSummary.gpuVendor = services.gpuVendor;
        startupSummary.gpuRenderer = services.gpuRenderer;
        startupSummary.gpuDiscrete = services.gpuDiscrete;
        startupSummary.vsyncEnabled = services.vsyncEnabled;
        startupSummary.hasOpenGlStrings = renderer->requiresOpenGLContext();
        if (startupSummary.hasOpenGlStrings) {
            startupSummary.glVersion = glStringOrUnknown(GL_VERSION);
            startupSummary.glslVersion = glStringOrUnknown(GL_SHADING_LANGUAGE_VERSION);
        }
        game::runtime::startup_diag::logActiveRendererSummary(startupSummary, std::cout);
        game::runtime::startup_diag::logPreferredActiveAdapterMismatch(
            services.preferredGpuAdapter,
            services.gpuRenderer,
            std::cout);

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
            // Ensure native backends show the same dark loading frame immediately,
            // avoiding a temporary OS white window before preload UI starts updating.
            renderBootLoading(0.0f);
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

    void GameRunner::syncVideoModeState() {
        updateDrawableSizeAndViewport();
        updateMouseScale();
        updateCameraAspect();
        if (renderer) {
            renderer->onResize(drawableW, drawableH);
        }
    }

    bool GameRunner::applyVideoMode(int width, int height, bool fullscreenWanted) {
        if (!window || !window->getSDLWindow()) return false;
        const auto requested =
            game::runtime::video_mode::sanitizeRequestedVideoMode(width, height, fullscreenWanted);
        const auto result = game::runtime::video_mode::applyRequestedVideoMode(
            window->getSDLWindow(),
            requested,
            std::cerr);
        if (!result.success) {
            return false;
        }
        fullscreen = result.fullscreen;
        syncVideoModeState();
        return true;
    }

    GameContext::VideoMode GameRunner::queryVideoMode() const {
        return game::runtime::video_mode::makeCurrentVideoMode(drawableW, drawableH, fullscreen);
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

        std::cout << "[Run] Main loop @ 60 Hz...\n";
        static const int maxFixedTicksPerFrame =
            game::runtime::loop_config::readMaxFixedTicksPerFrameFromEnvironment(std::cerr);
        std::cout << "[Run] Fixed tick budget: " << maxFixedTicksPerFrame << " ticks/frame\n";

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
        double perfAccumFixedTickWorkMs = 0.0;
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
        double perfAccumProjectedUnitsMs = 0.0;
        double perfAccumProjectedPoseEvalMs = 0.0;
        double perfAccumProjectedModelMs = 0.0;
        double perfAccumProjectedModelPrepMs = 0.0;
        double perfAccumProjectedModelGeometryMs = 0.0;
        double perfAccumProjectedOverlayMs = 0.0;
        double perfAccumProjectedUnitsProcessed = 0.0;
        double perfAccumProjectedModelUnits = 0.0;
        double perfAccumProjectedClipSkinnedUnits = 0.0;
        EngineRenderBuildBreakdown perfAccumRenderBreakdown{};
        EngineFixedPerfBreakdown perfAccumFixedBreakdown{};
        int perfAccumFixedTicks = 0;
        int perfAccumFixedTicksDropped = 0;
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

                if (game::runtime::sdl_input::isResizeWindowEvent(sdlEvent)) {
                    syncVideoModeState();
                    ctx.drawableW = drawableW;
                    ctx.drawableH = drawableH;
                }

                InputEvent e;
                game::runtime::sdl_input::TranslationContext inputContext;
                inputContext.mouseScaleX = mouseScaleX;
                inputContext.mouseScaleY = mouseScaleY;
                inputContext.windowW = windowW;
                inputContext.windowH = windowH;
                inputContext.drawableW = drawableW;
                inputContext.drawableH = drawableH;
                if (game::runtime::sdl_input::translateEvent(sdlEvent, inputContext, e)) {
                    game.handleEvent(e);
                }
            }

            auto now = clock::now();
            double frameDt = std::chrono::duration<double>(now - previous).count();
            frameDt = game::runtime::loop_config::clampFrameDeltaSeconds(frameDt);
            previous = now;

            accumulator += frameDt;

            services.frameFixedBreakdown = {};
            const auto frameCpuStart = clock::now();
            const auto fixedStart = frameCpuStart;
            double fixedTickWorkMsThisFrame = 0.0;
            int fixedTicksThisFrame = 0;
            int fixedTicksDroppedThisFrame = 0;
            while (accumulator >= TIME_STEP && fixedTicksThisFrame < maxFixedTicksPerFrame) {
                const auto fixedTickStart = clock::now();
                game.fixedUpdate(TIME_STEP);
                fixedTickWorkMsThisFrame +=
                    std::chrono::duration<double, std::milli>(clock::now() - fixedTickStart).count();
                accumulator -= TIME_STEP;
                ++fixedTicksThisFrame;
            }
            if (accumulator >= TIME_STEP) {
                fixedTicksDroppedThisFrame =
                    game::runtime::loop_config::dropExcessFixedTicks(accumulator, TIME_STEP);
            }
            const auto fixedEnd = clock::now();
            const EngineFixedPerfBreakdown fixedBreakdownThisFrame = services.frameFixedBreakdown;

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
            const float projectedUnitsMsThisFrame = services.frameProjectedUnitsMs;
            const float projectedPoseEvalMsThisFrame = services.frameProjectedPoseEvalMs;
            const float projectedModelMsThisFrame = services.frameProjectedModelMs;
            const float projectedModelPrepMsThisFrame = services.frameProjectedModelPrepMs;
            const float projectedModelGeometryMsThisFrame = services.frameProjectedModelGeometryMs;
            const float projectedOverlayMsThisFrame = services.frameProjectedOverlayMs;
            const std::uint32_t projectedUnitsProcessedThisFrame = services.frameProjectedUnitsProcessed;
            const std::uint32_t projectedModelUnitsThisFrame = services.frameProjectedModelUnits;
            const std::uint32_t projectedClipSkinnedUnitsThisFrame = services.frameProjectedClipSkinnedUnits;
            EngineRenderBuildBreakdown renderBreakdownThisFrame = services.frameRenderBuildBreakdown;

            if (renderer) {
                renderer->endFrame();
                IRenderBackend::BackendFrameTimings backendTimings;
                const bool hasBackendTimings = renderer->getLastFrameTimings(backendTimings);
                if (hasBackendTimings && backendTimings.gpuFrameValid) {
                    gpuFrameMs = std::max(0.0, static_cast<double>(backendTimings.gpuFrameMs));
                    gpuFrameValid = true;
                }
                if (renderer->handlesPresentation()) {
                    if (hasBackendTimings) {
                        presentWaitMs = std::max(0.0, static_cast<double>(backendTimings.presentWaitMs));
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
            const float renderAttributedMs =
                projectedUnitsMsThisFrame +
                renderBreakdownThisFrame.worldComposeMs +
                renderBreakdownThisFrame.overlayPrepMs +
                renderBreakdownThisFrame.worldBackgroundMs +
                renderBreakdownThisFrame.worldTriangles3dMs +
                renderBreakdownThisFrame.worldIndexedMs +
                renderBreakdownThisFrame.worldDebugMs +
                renderBreakdownThisFrame.spriteMs +
                renderBreakdownThisFrame.uiMs;
            renderBreakdownThisFrame.otherMs = std::max(
                0.0f,
                static_cast<float>(renderBuildMs) - renderAttributedMs);
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
            perfAccumFixedTickWorkMs += fixedTickWorkMsThisFrame;
            perfAccumFixedBreakdown.preUpdateMs += fixedBreakdownThisFrame.preUpdateMs;
            perfAccumFixedBreakdown.updatePhaseMs += fixedBreakdownThisFrame.updatePhaseMs;
            perfAccumFixedBreakdown.postUpdateMs += fixedBreakdownThisFrame.postUpdateMs;
            perfAccumFixedBreakdown.postOtherMs += fixedBreakdownThisFrame.postOtherMs;
            perfAccumFixedBreakdown.phaseTransitionMs += fixedBreakdownThisFrame.phaseTransitionMs;
            perfAccumFixedBreakdown.backendHydrateMs += fixedBreakdownThisFrame.backendHydrateMs;
            perfAccumFixedBreakdown.cameraMs += fixedBreakdownThisFrame.cameraMs;
            perfAccumFixedBreakdown.unitInteractionMs += fixedBreakdownThisFrame.unitInteractionMs;
            perfAccumFixedBreakdown.shopMs += fixedBreakdownThisFrame.shopMs;
            perfAccumFixedBreakdown.roundMs += fixedBreakdownThisFrame.roundMs;
            perfAccumFixedBreakdown.stateManagerMs += fixedBreakdownThisFrame.stateManagerMs;
            perfAccumFixedBreakdown.stateUpdateMs += fixedBreakdownThisFrame.stateUpdateMs;
            perfAccumFixedBreakdown.stateFlushMs += fixedBreakdownThisFrame.stateFlushMs;
            perfAccumFixedBreakdown.movementMs += fixedBreakdownThisFrame.movementMs;
            perfAccumFixedBreakdown.movementPlanMs += fixedBreakdownThisFrame.movementPlanMs;
            perfAccumFixedBreakdown.movementLuaMs += fixedBreakdownThisFrame.movementLuaMs;
            perfAccumFixedBreakdown.movementFlushMs += fixedBreakdownThisFrame.movementFlushMs;
            perfAccumFixedBreakdown.movementAdvanceMs += fixedBreakdownThisFrame.movementAdvanceMs;
            perfAccumFixedBreakdown.combatMs += fixedBreakdownThisFrame.combatMs;
            perfAccumFixedBreakdown.combatPlanMs += fixedBreakdownThisFrame.combatPlanMs;
            perfAccumFixedBreakdown.combatLuaMs += fixedBreakdownThisFrame.combatLuaMs;
            perfAccumFixedBreakdown.combatFlushMs += fixedBreakdownThisFrame.combatFlushMs;
            perfAccumFixedBreakdown.worldMs += fixedBreakdownThisFrame.worldMs;
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
            perfAccumProjectedUnitsMs += static_cast<double>(projectedUnitsMsThisFrame);
            perfAccumProjectedPoseEvalMs += static_cast<double>(projectedPoseEvalMsThisFrame);
            perfAccumProjectedModelMs += static_cast<double>(projectedModelMsThisFrame);
            perfAccumProjectedModelPrepMs += static_cast<double>(projectedModelPrepMsThisFrame);
            perfAccumProjectedModelGeometryMs += static_cast<double>(projectedModelGeometryMsThisFrame);
            perfAccumProjectedOverlayMs += static_cast<double>(projectedOverlayMsThisFrame);
            perfAccumProjectedUnitsProcessed += static_cast<double>(projectedUnitsProcessedThisFrame);
            perfAccumProjectedModelUnits += static_cast<double>(projectedModelUnitsThisFrame);
            perfAccumProjectedClipSkinnedUnits += static_cast<double>(projectedClipSkinnedUnitsThisFrame);
            perfAccumRenderBreakdown.worldComposeMs += renderBreakdownThisFrame.worldComposeMs;
            perfAccumRenderBreakdown.worldBackdropMs += renderBreakdownThisFrame.worldBackdropMs;
            perfAccumRenderBreakdown.worldVfxMs += renderBreakdownThisFrame.worldVfxMs;
            perfAccumRenderBreakdown.worldDepthFlushMs += renderBreakdownThisFrame.worldDepthFlushMs;
            perfAccumRenderBreakdown.overlayPrepMs += renderBreakdownThisFrame.overlayPrepMs;
            perfAccumRenderBreakdown.worldBackgroundMs += renderBreakdownThisFrame.worldBackgroundMs;
            perfAccumRenderBreakdown.worldTriangles3dMs += renderBreakdownThisFrame.worldTriangles3dMs;
            perfAccumRenderBreakdown.worldIndexedMs += renderBreakdownThisFrame.worldIndexedMs;
            perfAccumRenderBreakdown.worldDebugMs += renderBreakdownThisFrame.worldDebugMs;
            perfAccumRenderBreakdown.spriteMs += renderBreakdownThisFrame.spriteMs;
            perfAccumRenderBreakdown.uiMs += renderBreakdownThisFrame.uiMs;
            perfAccumRenderBreakdown.otherMs += renderBreakdownThisFrame.otherMs;
            perfAccumFixedTicks += fixedTicksThisFrame;
            perfAccumFixedTicksDropped += fixedTicksDroppedThisFrame;
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
                const double avgProjectedUnitsMs = perfAccumProjectedUnitsMs / frames;
                const double avgProjectedPoseEvalMs = perfAccumProjectedPoseEvalMs / frames;
                const double avgProjectedModelMs = perfAccumProjectedModelMs / frames;
                const double avgProjectedModelPrepMs = perfAccumProjectedModelPrepMs / frames;
                const double avgProjectedModelGeometryMs = perfAccumProjectedModelGeometryMs / frames;
                const double avgProjectedOverlayMs = perfAccumProjectedOverlayMs / frames;
                const std::uint32_t avgProjectedUnitsProcessed = static_cast<std::uint32_t>(
                    std::lround(perfAccumProjectedUnitsProcessed / frames));
                const std::uint32_t avgProjectedModelUnits = static_cast<std::uint32_t>(
                    std::lround(perfAccumProjectedModelUnits / frames));
                const std::uint32_t avgProjectedClipSkinnedUnits = static_cast<std::uint32_t>(
                    std::lround(perfAccumProjectedClipSkinnedUnits / frames));
                EngineRenderBuildBreakdown avgRenderBreakdown{};
                avgRenderBreakdown.worldComposeMs =
                    static_cast<float>(perfAccumRenderBreakdown.worldComposeMs / frames);
                avgRenderBreakdown.worldBackdropMs =
                    static_cast<float>(perfAccumRenderBreakdown.worldBackdropMs / frames);
                avgRenderBreakdown.worldVfxMs =
                    static_cast<float>(perfAccumRenderBreakdown.worldVfxMs / frames);
                avgRenderBreakdown.worldDepthFlushMs =
                    static_cast<float>(perfAccumRenderBreakdown.worldDepthFlushMs / frames);
                avgRenderBreakdown.overlayPrepMs =
                    static_cast<float>(perfAccumRenderBreakdown.overlayPrepMs / frames);
                avgRenderBreakdown.worldBackgroundMs =
                    static_cast<float>(perfAccumRenderBreakdown.worldBackgroundMs / frames);
                avgRenderBreakdown.worldTriangles3dMs =
                    static_cast<float>(perfAccumRenderBreakdown.worldTriangles3dMs / frames);
                avgRenderBreakdown.worldIndexedMs =
                    static_cast<float>(perfAccumRenderBreakdown.worldIndexedMs / frames);
                avgRenderBreakdown.worldDebugMs =
                    static_cast<float>(perfAccumRenderBreakdown.worldDebugMs / frames);
                avgRenderBreakdown.spriteMs =
                    static_cast<float>(perfAccumRenderBreakdown.spriteMs / frames);
                avgRenderBreakdown.uiMs =
                    static_cast<float>(perfAccumRenderBreakdown.uiMs / frames);
                avgRenderBreakdown.otherMs =
                    static_cast<float>(perfAccumRenderBreakdown.otherMs / frames);
                const int avgFixedTicks = static_cast<int>(std::lround(static_cast<double>(perfAccumFixedTicks) / frames));
                const int avgFixedTicksDropped =
                    static_cast<int>(std::lround(static_cast<double>(perfAccumFixedTicksDropped) / frames));
                const double avgFixedTickMs = perfAccumFixedTicks > 0
                    ? (perfAccumFixedTickWorkMs / static_cast<double>(perfAccumFixedTicks))
                    : 0.0;
                EngineFixedPerfBreakdown avgFixedBreakdown{};
                avgFixedBreakdown.preUpdateMs =
                    static_cast<float>(perfAccumFixedBreakdown.preUpdateMs / frames);
                avgFixedBreakdown.updatePhaseMs =
                    static_cast<float>(perfAccumFixedBreakdown.updatePhaseMs / frames);
                avgFixedBreakdown.postUpdateMs =
                    static_cast<float>(perfAccumFixedBreakdown.postUpdateMs / frames);
                avgFixedBreakdown.postOtherMs =
                    static_cast<float>(perfAccumFixedBreakdown.postOtherMs / frames);
                avgFixedBreakdown.phaseTransitionMs =
                    static_cast<float>(perfAccumFixedBreakdown.phaseTransitionMs / frames);
                avgFixedBreakdown.backendHydrateMs =
                    static_cast<float>(perfAccumFixedBreakdown.backendHydrateMs / frames);
                avgFixedBreakdown.cameraMs =
                    static_cast<float>(perfAccumFixedBreakdown.cameraMs / frames);
                avgFixedBreakdown.unitInteractionMs =
                    static_cast<float>(perfAccumFixedBreakdown.unitInteractionMs / frames);
                avgFixedBreakdown.shopMs =
                    static_cast<float>(perfAccumFixedBreakdown.shopMs / frames);
                avgFixedBreakdown.roundMs =
                    static_cast<float>(perfAccumFixedBreakdown.roundMs / frames);
                avgFixedBreakdown.stateManagerMs =
                    static_cast<float>(perfAccumFixedBreakdown.stateManagerMs / frames);
                avgFixedBreakdown.stateUpdateMs =
                    static_cast<float>(perfAccumFixedBreakdown.stateUpdateMs / frames);
                avgFixedBreakdown.stateFlushMs =
                    static_cast<float>(perfAccumFixedBreakdown.stateFlushMs / frames);
                avgFixedBreakdown.movementMs =
                    static_cast<float>(perfAccumFixedBreakdown.movementMs / frames);
                avgFixedBreakdown.movementPlanMs =
                    static_cast<float>(perfAccumFixedBreakdown.movementPlanMs / frames);
                avgFixedBreakdown.movementLuaMs =
                    static_cast<float>(perfAccumFixedBreakdown.movementLuaMs / frames);
                avgFixedBreakdown.movementFlushMs =
                    static_cast<float>(perfAccumFixedBreakdown.movementFlushMs / frames);
                avgFixedBreakdown.movementAdvanceMs =
                    static_cast<float>(perfAccumFixedBreakdown.movementAdvanceMs / frames);
                avgFixedBreakdown.combatMs =
                    static_cast<float>(perfAccumFixedBreakdown.combatMs / frames);
                avgFixedBreakdown.combatPlanMs =
                    static_cast<float>(perfAccumFixedBreakdown.combatPlanMs / frames);
                avgFixedBreakdown.combatLuaMs =
                    static_cast<float>(perfAccumFixedBreakdown.combatLuaMs / frames);
                avgFixedBreakdown.combatFlushMs =
                    static_cast<float>(perfAccumFixedBreakdown.combatFlushMs / frames);
                avgFixedBreakdown.worldMs =
                    static_cast<float>(perfAccumFixedBreakdown.worldMs / frames);

                services.framePerf.fps = static_cast<float>(fps);
                services.framePerf.frameMs = static_cast<float>(avgFrameMs);
                services.framePerf.fixedMs = static_cast<float>(avgFixedMs);
                services.framePerf.fixedTickMs = static_cast<float>(avgFixedTickMs);
                services.framePerf.renderBuildMs = static_cast<float>(avgRenderBuildMs);
                services.framePerf.renderSubmitMs = static_cast<float>(avgRenderSubmitMs);
                services.framePerf.presentWaitMs = static_cast<float>(avgPresentWaitMs);
                services.framePerf.gpuFrameMs = static_cast<float>(avgGpuFrameMs);
                services.framePerf.gpuFrameValid = hasGpuFrameAverage;
                services.framePerf.drawCalls = avgDrawCalls;
                services.framePerf.triangles = avgTriangles;
                services.framePerf.visibleAnimatedUnits = avgVisibleAnimatedUnits;
                services.framePerf.particleCount = avgParticleCount;
                services.framePerf.projectedUnitsMs = static_cast<float>(avgProjectedUnitsMs);
                services.framePerf.projectedPoseEvalMs = static_cast<float>(avgProjectedPoseEvalMs);
                services.framePerf.projectedModelMs = static_cast<float>(avgProjectedModelMs);
                services.framePerf.projectedModelPrepMs = static_cast<float>(avgProjectedModelPrepMs);
                services.framePerf.projectedModelGeometryMs = static_cast<float>(avgProjectedModelGeometryMs);
                services.framePerf.projectedOverlayMs = static_cast<float>(avgProjectedOverlayMs);
                services.framePerf.projectedUnitsProcessed = avgProjectedUnitsProcessed;
                services.framePerf.projectedModelUnits = avgProjectedModelUnits;
                services.framePerf.projectedClipSkinnedUnits = avgProjectedClipSkinnedUnits;
                services.framePerf.renderBreakdown = avgRenderBreakdown;
                services.framePerf.renderMs = static_cast<float>(avgLegacyRenderMs);
                services.framePerf.swapMs = static_cast<float>(avgLegacySwapMs);
                services.framePerf.fixedTicks = avgFixedTicks;
                services.framePerf.fixedTicksDropped = avgFixedTicksDropped;
                services.framePerf.fixedBreakdown = avgFixedBreakdown;

                std::ostringstream fixedSystemsLine;
                {
                    struct FixedSystemEntry {
                        const char* name;
                        float ms;
                    };
                    std::array<FixedSystemEntry, 10> fixedSystemEntries{{
                        {"backend_hydrate", avgFixedBreakdown.backendHydrateMs},
                        {"combat", avgFixedBreakdown.combatMs},
                        {"world", avgFixedBreakdown.worldMs},
                        {"movement", avgFixedBreakdown.movementMs},
                        {"round", avgFixedBreakdown.roundMs},
                        {"state", avgFixedBreakdown.stateManagerMs},
                        {"post_other", avgFixedBreakdown.postOtherMs},
                        {"phasechg", avgFixedBreakdown.phaseTransitionMs},
                        {"camera", avgFixedBreakdown.cameraMs},
                        {"unit", avgFixedBreakdown.unitInteractionMs},
                    }};
                    std::sort(
                        fixedSystemEntries.begin(),
                        fixedSystemEntries.end(),
                        [](const FixedSystemEntry& a, const FixedSystemEntry& b) {
                            return a.ms > b.ms;
                        });
                    int emittedFixedSystems = 0;
                    for (const auto& entry : fixedSystemEntries) {
                        if (entry.ms < 0.05f) continue;
                        fixedSystemsLine << (emittedFixedSystems == 0 ? " fsys=" : ",")
                                         << entry.name << ":" << entry.ms << "ms";
                        ++emittedFixedSystems;
                        if (emittedFixedSystems >= 3) break;
                    }
                }

                std::cout << std::fixed << std::setprecision(1)
                          << "[Perf] FPS=" << fps
                          << " frame=" << avgFrameMs << "ms"
                          << " fixed=" << avgFixedMs << "ms"
                          << " ftick=" << avgFixedTickMs << "ms"
                          << " build=" << avgRenderBuildMs << "ms"
                          << " submit=" << avgRenderSubmitMs << "ms"
                          << " present=" << avgPresentWaitMs << "ms"
                          << " gpu=" << (hasGpuFrameAverage ? avgGpuFrameMs : -1.0) << "ms"
                          << " draws=" << avgDrawCalls
                          << " tris=" << avgTriangles
                          << " units=" << avgVisibleAnimatedUnits
                          << " particles=" << avgParticleCount
                          << " proj=" << avgProjectedUnitsMs << "ms"
                          << " pose=" << avgProjectedPoseEvalMs << "ms"
                          << " model=" << avgProjectedModelMs << "ms"
                          << " prep=" << avgProjectedModelPrepMs << "ms"
                          << " geom=" << avgProjectedModelGeometryMs << "ms"
                          << " over=" << avgProjectedOverlayMs << "ms"
                          << " clipskin=" << avgProjectedClipSkinnedUnits
                          << " render=" << avgLegacyRenderMs << "ms"
                          << " swap=" << avgLegacySwapMs << "ms"
                          << " ticks=" << avgFixedTicks
                          << " drop=" << avgFixedTicksDropped
                          << fixedSystemsLine.str() << "\n";

                std::ostringstream perfJson;
                perfJson << std::fixed << std::setprecision(3)
                         << "[PerfJSON] {"
                         << "\"fps\":" << fps
                         << ",\"frame_cpu_ms\":" << avgFrameMs
                         << ",\"fixed_ms\":" << avgFixedMs
                         << ",\"fixed_tick_ms\":" << avgFixedTickMs
                         << ",\"render_build_ms\":" << avgRenderBuildMs
                         << ",\"render_submit_ms\":" << avgRenderSubmitMs
                         << ",\"present_wait_ms\":" << avgPresentWaitMs
                         << ",\"gpu_frame_ms\":" << (hasGpuFrameAverage ? avgGpuFrameMs : -1.0)
                         << ",\"gpu_frame_valid\":" << (hasGpuFrameAverage ? 1 : 0)
                         << ",\"draw_calls\":" << avgDrawCalls
                         << ",\"triangles\":" << avgTriangles
                         << ",\"visible_animated_units\":" << avgVisibleAnimatedUnits
                         << ",\"particle_count\":" << avgParticleCount
                         << ",\"projected_units_ms\":" << avgProjectedUnitsMs
                         << ",\"projected_pose_eval_ms\":" << avgProjectedPoseEvalMs
                         << ",\"projected_model_ms\":" << avgProjectedModelMs
                         << ",\"projected_model_prep_ms\":" << avgProjectedModelPrepMs
                          << ",\"projected_model_geometry_ms\":" << avgProjectedModelGeometryMs
                          << ",\"projected_overlay_ms\":" << avgProjectedOverlayMs
                          << ",\"projected_units_processed\":" << avgProjectedUnitsProcessed
                          << ",\"projected_model_units\":" << avgProjectedModelUnits
                          << ",\"projected_clip_skinned_units\":" << avgProjectedClipSkinnedUnits
                          << ",\"render_world_compose_ms\":" << avgRenderBreakdown.worldComposeMs
                          << ",\"render_world_backdrop_ms\":" << avgRenderBreakdown.worldBackdropMs
                          << ",\"render_world_vfx_ms\":" << avgRenderBreakdown.worldVfxMs
                          << ",\"render_world_depth_flush_ms\":" << avgRenderBreakdown.worldDepthFlushMs
                          << ",\"render_overlay_prep_ms\":" << avgRenderBreakdown.overlayPrepMs
                          << ",\"render_world_background_ms\":" << avgRenderBreakdown.worldBackgroundMs
                          << ",\"render_world_triangles_3d_ms\":" << avgRenderBreakdown.worldTriangles3dMs
                          << ",\"render_world_indexed_ms\":" << avgRenderBreakdown.worldIndexedMs
                          << ",\"render_world_debug_ms\":" << avgRenderBreakdown.worldDebugMs
                          << ",\"render_sprite_submit_ms\":" << avgRenderBreakdown.spriteMs
                          << ",\"render_ui_submit_ms\":" << avgRenderBreakdown.uiMs
                          << ",\"render_other_ms\":" << avgRenderBreakdown.otherMs
                          << ",\"legacy_render_ms\":" << avgLegacyRenderMs
                          << ",\"legacy_swap_ms\":" << avgLegacySwapMs
                          << ",\"fixed_ticks\":" << avgFixedTicks
                         << ",\"fixed_phase_pre_ms\":" << avgFixedBreakdown.preUpdateMs
                         << ",\"fixed_phase_update_ms\":" << avgFixedBreakdown.updatePhaseMs
                         << ",\"fixed_phase_post_ms\":" << avgFixedBreakdown.postUpdateMs
                         << ",\"fixed_phase_post_other_ms\":" << avgFixedBreakdown.postOtherMs
                         << ",\"fixed_phase_transition_ms\":" << avgFixedBreakdown.phaseTransitionMs
                         << ",\"fixed_backend_hydrate_ms\":" << avgFixedBreakdown.backendHydrateMs
                         << ",\"fixed_camera_ms\":" << avgFixedBreakdown.cameraMs
                         << ",\"fixed_unit_interaction_ms\":" << avgFixedBreakdown.unitInteractionMs
                         << ",\"fixed_shop_ms\":" << avgFixedBreakdown.shopMs
                         << ",\"fixed_round_ms\":" << avgFixedBreakdown.roundMs
                         << ",\"fixed_state_manager_ms\":" << avgFixedBreakdown.stateManagerMs
                         << ",\"fixed_state_update_ms\":" << avgFixedBreakdown.stateUpdateMs
                         << ",\"fixed_state_flush_ms\":" << avgFixedBreakdown.stateFlushMs
                         << ",\"fixed_movement_ms\":" << avgFixedBreakdown.movementMs
                         << ",\"fixed_movement_plan_ms\":" << avgFixedBreakdown.movementPlanMs
                         << ",\"fixed_movement_lua_ms\":" << avgFixedBreakdown.movementLuaMs
                         << ",\"fixed_movement_flush_ms\":" << avgFixedBreakdown.movementFlushMs
                         << ",\"fixed_movement_advance_ms\":" << avgFixedBreakdown.movementAdvanceMs
                         << ",\"fixed_combat_ms\":" << avgFixedBreakdown.combatMs
                         << ",\"fixed_combat_plan_ms\":" << avgFixedBreakdown.combatPlanMs
                         << ",\"fixed_combat_lua_ms\":" << avgFixedBreakdown.combatLuaMs
                         << ",\"fixed_combat_flush_ms\":" << avgFixedBreakdown.combatFlushMs
                         << ",\"fixed_world_ms\":" << avgFixedBreakdown.worldMs
                         << ",\"fixed_ticks_dropped\":" << avgFixedTicksDropped
                         << "}";
                std::cout << perfJson.str() << "\n";

                frameCount = 0;
                fpsTimer = 0.0;
                perfAccumFrameMs = 0.0;
                perfAccumFixedMs = 0.0;
                perfAccumFixedTickWorkMs = 0.0;
                perfAccumFixedBreakdown = {};
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
                perfAccumProjectedUnitsMs = 0.0;
                perfAccumProjectedPoseEvalMs = 0.0;
                perfAccumProjectedModelMs = 0.0;
                perfAccumProjectedModelPrepMs = 0.0;
                perfAccumProjectedModelGeometryMs = 0.0;
                perfAccumProjectedOverlayMs = 0.0;
                perfAccumProjectedUnitsProcessed = 0.0;
                perfAccumProjectedModelUnits = 0.0;
                perfAccumProjectedClipSkinnedUnits = 0.0;
                perfAccumRenderBreakdown = {};
                perfAccumFixedTicks = 0;
                perfAccumFixedTicksDropped = 0;
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
        if (!game::runtime::restart_policy::clearStaleRestartRequest(prefsPath, std::cerr)) {
            return 1;
        }

        GameRunner runner;
        if (!runner.init()) {
            runner.shutdown();
            return 1;
        }

        GameApp app;
        lastResult = runner.run(app);

        runner.shutdown();

        bool shouldRelaunch = false;
        if (!game::runtime::restart_policy::consumeRestartRequestForRelaunch(
                prefsPath,
                std::cerr,
                shouldRelaunch)) {
            return lastResult;
        }
        if (!shouldRelaunch) {
            return lastResult;
        }

        std::cout << "[Run] Restart requested. Re-launching game session...\n";
    }
}

} // namespace game

