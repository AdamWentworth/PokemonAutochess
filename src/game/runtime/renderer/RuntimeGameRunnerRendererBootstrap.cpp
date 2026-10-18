#include "ProjectRendererPolicy.h"
#include "game/runtime/renderer/RuntimeGameRunnerRendererBootstrap.h"

#include "game/runtime/GameRuntimeServices.h"
#include "engine/platform/Window.h"
#include "engine/render/IRenderBackend.h"
#include "game/runtime/RuntimeOpenGlBootstrap.h"
#include "game/runtime/renderer/RendererBackendBootstrap.h"
#include "game/runtime/renderer/RuntimeRendererRecovery.h"
#include "game/runtime/video/RuntimeWindowBootstrap.h"
#include "game/runtime/video/RuntimeWindowPresentationController.h"

namespace game::runtime::runner_renderer_bootstrap {

game::runtime::renderer_recovery::Result createWithFallbacks(
    game::video::RendererBackend activeBackend,
    const std::string &activeBackendName,
    GameRuntimeServices &services,
    std::unique_ptr<Window> &window,
    game::runtime::window_presentation::WindowPresentationController &presentation,
    const std::function<bool(std::string *)> &loadOpenGlFunctions,
    int fallbackWindowWidth,
    int fallbackWindowHeight) {
    game::runtime::renderer_recovery::Inputs inputs{activeBackend, activeBackendName, {}};
    if (services.requestedRendererBackend == "auto") {
        for (const auto &name : engine::render::availableRenderers(projectRendererPolicy()))
            inputs.fallbackBackends.push_back(game::video::parseRendererBackend(name));
    }
    return game::runtime::renderer_recovery::createWithFallbacks(
        inputs,
        [&window, &presentation, &services](game::video::RendererBackend backend, std::string *outError) {
            return game::runtime::backend_bootstrap::createRenderBackend(
                backend,
                window ? window->getSDLWindow() : nullptr,
                presentation.drawableWidth(),
                presentation.drawableHeight(),
                services.vsyncEnabled,
                services.preferredGpuAdapter,
                outError);
        },
        [&window,
         &presentation,
         &services,
         fallbackWindowWidth,
         fallbackWindowHeight](game::video::RendererBackend backend) {
            window.reset();
            const auto fallbackWindow = game::runtime::window_bootstrap::openWindow(
                game::runtime::window_bootstrap::OpenRequest{
                    game::runtime::backend_bootstrap::graphicsApiForBackend(backend),
                    services.vsyncEnabled},
                [&window, fallbackWindowWidth, fallbackWindowHeight](
                    Window::GraphicsApi graphicsApi,
                    bool vsyncEnabled) {
                    window = std::make_unique<Window>(
                        "Pokemon Autochess",
                        fallbackWindowWidth,
                        fallbackWindowHeight,
                        graphicsApi,
                        vsyncEnabled);
                },
                [&window]() {
                    return window && window->hasOpenGLContext();
                });
            presentation.bindWindow(window.get());
            presentation.setOpenGlState(
                fallbackWindow.hasOpenGlContext,
                fallbackWindow.glFunctionsReady);
            return game::runtime::renderer_recovery::FallbackWindowResult{
                fallbackWindow.success,
                fallbackWindow.error};
        },
        [&presentation, &loadOpenGlFunctions](std::string *outError) {
            const bool ok = game::runtime::opengl_bootstrap::initializeOpenGlFunctions(
                loadOpenGlFunctions,
                outError);
            presentation.setOpenGlState(presentation.hasOpenGlContext(), ok);
            return ok;
        },
        [&presentation]() {
            presentation.syncVideoModeState();
        });
}

} // namespace game::runtime::runner_renderer_bootstrap
