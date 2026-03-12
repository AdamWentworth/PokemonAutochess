// src/game/GameRunner.cpp

#include "game/runtime/GameRunner.h"

#include "game/runtime/GameApp.h"

#include "engine/core/EngineServices.h"
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
#include "game/runtime/RendererBackendBootstrap.h"
#include "game/runtime/RuntimeBootLoading.h"
#include "game/runtime/RuntimeFixedStepPhase.h"
#include "game/runtime/RuntimeFrameObservation.h"
#include "game/runtime/RuntimeFramePerfCapture.h"
#include "game/runtime/RuntimeLoopConfig.h"
#include "game/runtime/RuntimeLoopControl.h"
#include "game/runtime/RuntimePerfAccumulator.h"
#include "game/runtime/RuntimePerfLogging.h"
#include "game/runtime/RuntimeRelaunchLoop.h"
#include "game/runtime/RuntimeRendererActivation.h"
#include "game/runtime/RuntimeRendererRecovery.h"
#include "game/runtime/RuntimeSdlEventDispatch.h"
#include "game/runtime/RuntimeSdlInput.h"
#include "game/runtime/RuntimeSdlVideoMode.h"
#include "game/runtime/RuntimeStartupConfig.h"
#include "game/runtime/RuntimeStartupSession.h"
#include "game/runtime/RuntimeStartupVideoOverride.h"
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
        const auto startupSession =
            game::runtime::startup_session::prepareFromEnvironment(prefsPath, std::cout, std::cerr);
        requestedBackend = startupSession.requestedBackend;
        activeBackend = startupSession.activeBackend;
        game::runtime::startup_session::applyToServices(startupSession, services);

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

        const auto startupOverrideResult = game::runtime::startup_video_override::apply(
            game::runtime::startup_config::readStartupVideoOverride(std::cerr),
            [this]() { return this->queryVideoMode(); },
            [this](int width, int height, bool isFullscreen) {
                return this->applyVideoMode(width, height, isFullscreen);
            });
        if (startupOverrideResult.attempted) {
            (startupOverrideResult.applied ? std::cout : std::cerr) << startupOverrideResult.message << "\n";
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
                try {
                    window = std::make_unique<Window>(
                        "Pokemon Autochess",
                        static_cast<int>(START_W),
                        static_cast<int>(START_H),
                        Window::GraphicsApi::OpenGL,
                        services.vsyncEnabled);
                    windowHasOpenGLContext = window->hasOpenGLContext();
                    glFunctionsReady = false;
                    return game::runtime::renderer_recovery::OpenGlWindowResult{true, {}};
                } catch (const std::exception& ex) {
                    return game::runtime::renderer_recovery::OpenGlWindowResult{false, ex.what()};
                }
            },
            [this](std::string* outError) {
                if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
                    if (outError) *outError = "gladLoadGLLoader failed";
                    return false;
                }
                glFunctionsReady = true;
                return true;
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

        game::runtime::renderer_activation::Inputs activationInputs;
        activationInputs.requestedBackend = services.requestedRendererBackend;
        activationInputs.preferredGpuAdapter = services.preferredGpuAdapter;
        activationInputs.vsyncEnabled = services.vsyncEnabled;
        activationInputs.requireDiscreteGpu = services.requireDiscreteGpu;
        activationInputs.rendererRequiresOpenGlContext = renderer->requiresOpenGLContext();
        activationInputs.rendererBackendId = renderer->backendId();
        activationInputs.activeGpuName = renderer->activeGpuName();
        activationInputs.activeGpuIsDiscrete = renderer->activeGpuIsDiscrete();
        if (activationInputs.rendererRequiresOpenGlContext) {
            activationInputs.glVendor = glStringOrUnknown(GL_VENDOR);
            activationInputs.glRenderer = glStringOrUnknown(GL_RENDERER);
            activationInputs.glVersion = glStringOrUnknown(GL_VERSION);
            activationInputs.glslVersion = glStringOrUnknown(GL_SHADING_LANGUAGE_VERSION);
        }

        const auto activation = game::runtime::renderer_activation::resolve(activationInputs);
        services.activeRendererBackend = activation.activeBackend;
        services.gpuVendor = activation.gpuVendor;
        services.gpuRenderer = activation.gpuRenderer;
        services.gpuDiscrete = activation.gpuDiscrete;
        game::runtime::renderer_activation::logStartupSummary(activationInputs, activation, std::cout);
        game::runtime::renderer_activation::logPreferredAdapterMismatch(
            activationInputs,
            activation,
            std::cout);

        if (!activation.discreteRequirementSatisfied) {
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
            SDL_Event sdlEvent;

            while (SDL_PollEvent(&sdlEvent)) {
                game::runtime::sdl_event_dispatch::Callbacks eventCallbacks;
                eventCallbacks.onResize = [this, &ctx]() {
                    syncVideoModeState();
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

            accumulator += frameDt;

            const auto frameCpuStart = clock::now();
            const auto fixedPhase = game::runtime::fixed_step_phase::execute(
                accumulator,
                TIME_STEP,
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

