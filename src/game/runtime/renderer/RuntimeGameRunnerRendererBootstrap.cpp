#include "game/runtime/renderer/RuntimeGameRunnerRendererBootstrap.h"

#include "engine/core/EngineServices.h"
#include "engine/platform/Window.h"
#include "engine/render/IRenderBackend.h"
#include "game/runtime/RuntimeOpenGlBootstrap.h"
#include "game/runtime/renderer/RendererBackendBootstrap.h"
#include "game/runtime/renderer/RuntimeRendererRecovery.h"
#include "game/runtime/video/RuntimeWindowBootstrap.h"
#include "game/runtime/video/RuntimeWindowPresentationController.h"

namespace game::runtime::runner_renderer_bootstrap {

game::runtime::renderer_recovery::Result createWithOpenGlFallback(
    game::video::RendererBackend activeBackend,
    const std::string& activeBackendName,
    EngineServices& services,
    std::unique_ptr<Window>& window,
    game::runtime::window_presentation::WindowPresentationController& presentation,
    const std::function<bool(std::string*)>& loadOpenGlFunctions,
    int fallbackWindowWidth,
    int fallbackWindowHeight) {
    return game::runtime::renderer_recovery::createWithOpenGlFallback(
        game::runtime::renderer_recovery::Inputs{activeBackend, activeBackendName},
        [&window, &presentation, &services](game::video::RendererBackend backend, std::string* outError) {
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
         fallbackWindowHeight]() {
            window.reset();
            const auto fallbackWindow = game::runtime::window_bootstrap::openWindow(
                game::runtime::window_bootstrap::OpenRequest{
                    Window::GraphicsApi::OpenGL,
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
            return game::runtime::renderer_recovery::OpenGlWindowResult{
                fallbackWindow.success,
                fallbackWindow.error};
        },
        [&presentation, &loadOpenGlFunctions](std::string* outError) {
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
