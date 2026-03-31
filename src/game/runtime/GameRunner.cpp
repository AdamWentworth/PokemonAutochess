// src/game/GameRunner.cpp

#include "game/runtime/GameApp.h"

#include "engine/core/EngineServices.h"
#include "engine/runtime/FixedStep.h"
#include "engine/core/GameContext.h"
#include "engine/core/GameLoop.h"
#include "engine/events/EventBus.h"
#include "engine/platform/Window.h"
#include "engine/render/Camera3D.h"
#include "engine/render/IRenderBackend.h"
#include "engine/ui/BootLoadingView.h"
#include "engine/utils/LogSink.h"
#include "engine/utils/ResourceManager.h"
#include "engine/utils/ShaderCache.h"
#include "game/runtime/AutoQuitPolicy.h"
#include "game/runtime/RuntimeBootLoading.h"
#include "game/runtime/RuntimeGameRunnerSession.h"
#include "game/runtime/loop/RuntimeGameRunnerEventPump.h"
#include "game/runtime/loop/RuntimeGameRunnerFrameDiagnostics.h"
#include "game/runtime/loop/RuntimeGameRunnerFrameExecution.h"
#include "game/runtime/loop/RuntimeGameRunnerLoopPolicy.h"
#include "game/runtime/loop/RuntimeLoopControl.h"
#include "game/runtime/RuntimeOpenGlBootstrap.h"
#include "game/runtime/renderer/RuntimeGameRunnerRendererBootstrap.h"
#include "game/runtime/renderer/RuntimeRendererRecovery.h"
#include "game/runtime/startup/RuntimeGameRunnerStartupFinalize.h"
#include "game/runtime/startup/RuntimeStartupSession.h"
#include "game/runtime/video/RuntimeGameRunnerWindowBootstrap.h"
#include "game/runtime/video/RuntimeSdlInput.h"
#include "game/runtime/video/RuntimeWindowPresentationController.h"
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
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace {
    constexpr unsigned int START_W  = 1280;
    constexpr unsigned int START_H  = 720;

    class GameRunner {
    public:
        GameRunner()
            : log_("GameRunner", &std::cout, &std::cerr)
            , presentation(services, std::cout, std::cerr) {}

        bool init();
        int run(GameLoop& game);
        void shutdown();

    private:
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
        engine::log::Sink log_;
        game::runtime::window_presentation::WindowPresentationController presentation;

        bool initialized = false;
        game::video::RendererBackend requestedBackend = game::video::RendererBackend::Auto;
        game::video::RendererBackend activeBackend = game::video::RendererBackend::OpenGL;
    };

    bool GameRunner::init() {
        const std::string prefsPath = game::video::defaultPreferencesPath();
        const auto startupSession =
            game::runtime::startup_session::prepareFromEnvironment(prefsPath, std::cout, std::cerr);
        requestedBackend = startupSession.requestedBackend;
        activeBackend = startupSession.activeBackend;
        game::runtime::startup_session::applyToServices(startupSession, services);
        presentation.setPreferencesPath(prefsPath);
        services.videoPreferencesPath = prefsPath;
        presentation.setAppliedVsyncEnabled(services.vsyncEnabled);

        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            log_.error(std::string("[GameRunner] SDL_Init failed: ") + SDL_GetError());
            return false;
        }

        const auto startupWindow = game::runtime::runner_window_bootstrap::openAndApplyStartupWindow(
            prefsPath,
            activeBackend,
            services.vsyncEnabled,
            window,
            presentation,
            std::cout,
            std::cerr);
        if (!startupWindow.success) {
            log_.error("[GameRunner] Window init failed: " + startupWindow.error);
            return false;
        }

        const auto preloadBootstrap = game::runtime::opengl_bootstrap::bootstrapLoadingPresentation(
            presentation.hasOpenGlContext(),
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
            std::string message = "[GameRunner] Failed to initialize GLAD";
            if (!preloadBootstrap.error.empty()) {
                message += ": " + preloadBootstrap.error;
            }
            log_.error(message);
            return false;
        }
        presentation.setOpenGlState(presentation.hasOpenGlContext(), preloadBootstrap.glFunctionsReady);

        auto rendererResult = game::runtime::runner_renderer_bootstrap::createWithOpenGlFallback(
            activeBackend,
            services.activeRendererBackend,
            services,
            window,
            presentation,
            [this](std::string* loaderError) {
                if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
                    if (loaderError) *loaderError = "gladLoadGLLoader failed";
                    return false;
                }
                return true;
            },
            static_cast<int>(START_W),
            static_cast<int>(START_H));
        if (rendererResult.rendererBackendFallback) {
            services.rendererBackendFallback = true;
            services.rendererBackendFallbackReason = rendererResult.rendererBackendFallbackReason;
            log_.info("[Renderer] " + services.rendererBackendFallbackReason);
        }
        activeBackend = rendererResult.activeBackend;
        services.activeRendererBackend = rendererResult.activeBackendName;
        renderer = std::move(rendererResult.renderer);
        presentation.bindRenderer(renderer.get());
        if (!renderer) {
            switch (rendererResult.failureStage) {
            case game::runtime::renderer_recovery::FailureStage::FallbackWindowOpen:
                log_.error("[Renderer] OpenGL fallback window init failed: " +
                           rendererResult.error);
                return false;
            case game::runtime::renderer_recovery::FailureStage::FallbackOpenGlInit:
                {
                    std::string message =
                        "[GameRunner] Failed to initialize GLAD after fallback";
                    if (!rendererResult.error.empty()) {
                        message += ": " + rendererResult.error;
                    }
                    log_.error(message);
                }
                return false;
            case game::runtime::renderer_recovery::FailureStage::InitialBackendCreate:
            case game::runtime::renderer_recovery::FailureStage::FallbackBackendCreate:
                log_.error("[Renderer] Failed to create backend '" +
                           services.activeRendererBackend + "' (" +
                           rendererResult.error + ").");
                return false;
            case game::runtime::renderer_recovery::FailureStage::None:
            default:
                log_.error("[Renderer] Failed to create backend '" +
                           services.activeRendererBackend + "'.");
                return false;
            }
        }

        const auto startupFinalize =
            game::runtime::runner_startup_finalize::activateRendererAndInitializePresentation(
                *renderer,
                services,
                presentation,
                camera,
                [this](float progress01) {
                    // Ensure native backends show the same dark loading frame immediately,
                    // avoiding a temporary OS white window before preload UI starts updating.
                    renderBootLoading(progress01);
                },
                std::cout,
                std::cerr);
        if (!startupFinalize.success) {
            log_.error(startupFinalize.error);
            return false;
        }

        initialized = true;
        log_.info("[Init] Game runner initialized.");
        return true;
    }

    void GameRunner::shutdown() {
        log_.info("[Shutdown] Game runner...");

        presentation.saveVideoModePreferences();

        if (renderer) {
            renderer->shutdown();
            presentation.bindRenderer(nullptr);
            renderer.reset();
        }

        shaderCache.clear();

        resourceManager.clear();
        bootLoadingView.reset();
        presentation.bindCamera(nullptr);
        camera.reset();

        presentation.bindWindow(nullptr);
        window.reset();

        if (SDL_WasInit(SDL_INIT_EVERYTHING) != 0) {
            TTF_Quit();
            SDL_Quit();
        }

        initialized = false;
        log_.info("[Shutdown] Game runner done.");
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
                presentation.syncVideoModeState();
                presentation.noteCurrentWindowModeChanged(false);
            }
        }
        return true;
    }

    void GameRunner::renderBootLoading(float progress01) {
        presentation.syncVideoModeState();
        if (bootLoadingView) {
            bootLoadingView->render(
                progress01,
                presentation.drawableWidth(),
                presentation.drawableHeight());
            swapBuffers();
        } else if (renderer) {
            renderer->beginFrame(0.05f, 0.05f, 0.07f, 1.0f);
            std::array<IRenderBackend::DebugQuad, game::runtime::boot_loading::kFallbackLoadingQuadCount> quads{};
            if (game::runtime::boot_loading::buildFallbackLoadingQuads(
                    presentation.drawableWidth(),
                    presentation.drawableHeight(),
                    progress01,
                    quads)) {
                renderer->drawDebugQuads(
                    quads.data(),
                    static_cast<int>(quads.size()),
                    presentation.drawableWidth(),
                    presentation.drawableHeight());
            }
            renderer->endFrame();
        }
    }

    int GameRunner::run(GameLoop& game) {
        if (!initialized) return 1;

        std::ostringstream line;
        line << "[Run] Main loop @ "
             << static_cast<int>(engine::runtime::fixed_step::kHz)
             << " Hz...";
        log_.info(line.str());

        game::runtime::loop_control::State loopState;
        services.resources = &resourceManager;
        services.shaders = &shaderCache;
        services.events = &eventBus;

        GameContext ctx;
        ctx.renderer = renderer.get();
        ctx.camera   = camera.get();
        ctx.services = &services;
        ctx.drawableW = presentation.drawableWidth();
        ctx.drawableH = presentation.drawableHeight();

        ctx.setTitle = [this](const std::string& t) { this->setTitle(t); };
        ctx.swapBuffers = [this]() { this->swapBuffers(); };
        ctx.requestQuit = [&loopState]() {
            game::runtime::loop_control::requestStop(loopState, "requestQuit() callback invoked");
        };
        ctx.pumpPreloadEvents = [this]() { return this->pumpPreloadEvents(); };
        ctx.renderBootLoading = [this](float p) { this->renderBootLoading(p); };
        ctx.applyVideoMode = [this, &ctx](int width, int height, bool isFullscreen) {
            const bool ok = presentation.applyVideoMode(width, height, isFullscreen);
            ctx.drawableW = presentation.drawableWidth();
            ctx.drawableH = presentation.drawableHeight();
            return ok;
        };
        ctx.queryVideoMode = [this]() { return presentation.queryVideoMode(); };

        game.init(ctx);

        if (!game::runtime::loop_control::isRunning(loopState)) {
            game.shutdown();
            return 0;
        }

        auto loopPolicy = game::runtime::runner_loop_policy::makeInitialState(
            game::runtime::runner_loop_policy::readConfig(std::cout, std::cerr));
        auto frameDiagnostics =
            game::runtime::runner_frame_diagnostics::makeInitialState(services);

        while (game::runtime::loop_control::isRunning(loopState)) {
            presentation.syncLivePresentationSettings();
            presentation.normalizeWindowedPresentationMode();
            game::runtime::runner_event_pump::pumpWindowEvents(
                presentation,
                ctx,
                game,
                loopState);

            const auto frameStart =
                game::runtime::runner_loop_policy::beginFrame(loopPolicy);

            const auto frameExecution =
                game::runtime::runner_frame_execution::execute({
                    .accumulator = game::runtime::runner_loop_policy::accumulator(loopPolicy),
                    .frameDt = frameStart.frameDt,
                    .maxFixedTicksPerFrame =
                        game::runtime::runner_loop_policy::maxFixedTicksPerFrame(loopPolicy),
                    .drawableW = presentation.drawableWidth(),
                    .drawableH = presentation.drawableHeight(),
                    .services = services,
                    .game = game,
                    .renderer = renderer.get(),
                    .swapBuffers = [this]() { this->swapBuffers(); },
                });
            game::runtime::runner_loop_policy::setAccumulator(
                loopPolicy,
                frameExecution.accumulator);

            game::runtime::runner_frame_diagnostics::Inputs frameDiagnosticInputs;
            frameDiagnosticInputs.frameDt = frameStart.frameDt;
            frameDiagnosticInputs.frameCpuMs = frameExecution.frameCpuMs;
            frameDiagnosticInputs.beginFrameMs = frameExecution.beginFrameMs;
            frameDiagnosticInputs.renderBuildMs = frameExecution.renderBuildMs;
            frameDiagnosticInputs.submitRawMs = frameExecution.submitRawMs;
            frameDiagnosticInputs.rendererHandlesPresentation =
                frameExecution.rendererHandlesPresentation;
            frameDiagnosticInputs.fixedPhase = frameExecution.fixedPhase;
            frameDiagnosticInputs.serviceSnapshot = frameExecution.serviceSnapshot;
            frameDiagnosticInputs.backendPerf = frameExecution.backendPerf;
            game::runtime::runner_frame_diagnostics::observeAndEmit(
                frameDiagnostics,
                services,
                frameDiagnosticInputs,
                std::cout);

            game::runtime::runner_loop_policy::finishFrame(
                loopPolicy,
                loopState,
                frameStart.frameDt,
                frameStart.frameStart,
                [this](const auto& startedAt) { this->enforceFrameCap(startedAt); });
        }

        game::runtime::runner_loop_policy::logExit(loopState, std::cout);
        game.shutdown();
        return 0;
    }
} // namespace

namespace game::runtime::runner_session {

int runSingleSession() {
    GameRunner runner;
    if (!runner.init()) {
        runner.shutdown();
        return 1;
    }

    GameApp app;
    const int lastResult = runner.run(app);

    runner.shutdown();
    return lastResult;
}

} // namespace game::runtime::runner_session


