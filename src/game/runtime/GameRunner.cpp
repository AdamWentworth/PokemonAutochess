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
#include "game/runtime/RuntimeFramePerfCapture.h"
#include "game/runtime/RuntimeBootLoading.h"
#include "game/runtime/RuntimeLoopConfig.h"
#include "game/runtime/RuntimePerfAccumulator.h"
#include "game/runtime/RuntimePerfLogging.h"
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
#include <iostream>
#include <memory>
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

        game::runtime::perf_accum::RollingAccumulator perfAccumulator;
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
            const EngineRenderBuildBreakdown rawRenderBreakdownThisFrame = services.frameRenderBuildBreakdown;

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

            const double fixedMs = std::chrono::duration<double, std::milli>(fixedEnd - fixedStart).count();
            const double beginFrameMs =
                std::chrono::duration<double, std::milli>(renderBuildStart - beginFrameStart).count();
            const double renderBuildMs =
                std::chrono::duration<double, std::milli>(renderBuildEnd - renderBuildStart).count();
            const auto renderBreakdownThisFrame =
                game::runtime::frame_perf_capture::finalizeRenderBreakdown(
                    renderBuildMs,
                    projectedUnitsMsThisFrame,
                    rawRenderBreakdownThisFrame);
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
            ++renderedFrames;
            elapsedSeconds += frameDt;

            game::runtime::perf_accum::FrameSample perfSample;
            perfSample.frameDt = frameDt;
            perfSample.frameCpuMs = frameCpuMs;
            perfSample.fixedMs = fixedMs;
            perfSample.fixedTickWorkMs = fixedTickWorkMsThisFrame;
            perfSample.renderBuildMs = renderBuildMs;
            perfSample.renderSubmitMs = submitMs;
            perfSample.presentWaitMs = totalPresentWaitMs;
            perfSample.legacyRenderMs = legacyRenderMs;
            perfSample.legacySwapMs = legacySwapMs;
            perfSample.gpuFrameMs = backendPerf.gpuFrameMs;
            perfSample.gpuFrameValid = backendPerf.gpuFrameValid;
            perfSample.drawCalls = backendPerf.drawCalls;
            perfSample.triangles = backendPerf.triangles;
            perfSample.visibleAnimatedUnits = visibleAnimatedUnitsThisFrame;
            perfSample.particleCount = particleCountThisFrame;
            perfSample.projectedUnitsMs = projectedUnitsMsThisFrame;
            perfSample.projectedPoseEvalMs = projectedPoseEvalMsThisFrame;
            perfSample.projectedModelMs = projectedModelMsThisFrame;
            perfSample.projectedModelPrepMs = projectedModelPrepMsThisFrame;
            perfSample.projectedModelGeometryMs = projectedModelGeometryMsThisFrame;
            perfSample.projectedOverlayMs = projectedOverlayMsThisFrame;
            perfSample.projectedUnitsProcessed = projectedUnitsProcessedThisFrame;
            perfSample.projectedModelUnits = projectedModelUnitsThisFrame;
            perfSample.projectedClipSkinnedUnits = projectedClipSkinnedUnitsThisFrame;
            perfSample.renderBreakdown = renderBreakdownThisFrame;
            perfSample.fixedBreakdown = fixedBreakdownThisFrame;
            perfSample.fixedTicks = fixedTicksThisFrame;
            perfSample.fixedTicksDropped = fixedTicksDroppedThisFrame;
            perfAccumulator.addFrame(perfSample);
            if (perfAccumulator.readyToEmit()) {
                const auto perfSummary = perfAccumulator.makeSummaryAndReset();
                services.framePerf = perfSummary.framePerf;
                std::cout << game::runtime::perf_logging::formatPerfLine(services.framePerf) << "\n";
                std::cout << game::runtime::perf_logging::formatPerfJson(services.framePerf) << "\n";
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

