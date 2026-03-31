#include "game/runtime/video/RuntimeGameRunnerWindowBootstrap.h"

#include "engine/platform/Window.h"
#include "game/runtime/renderer/RendererBackendBootstrap.h"
#include "game/runtime/startup/RuntimeStartupConfig.h"
#include "game/runtime/startup/RuntimeStartupVideoOverride.h"
#include "game/runtime/video/RuntimeSdlVideoMode.h"
#include "game/runtime/video/RuntimeWindowBootstrap.h"
#include "game/runtime/video/RuntimeWindowPresentationController.h"

#include <SDL2/SDL.h>

#include <ostream>

namespace game::runtime::runner_window_bootstrap {

Result openAndApplyStartupWindow(
    const std::string& prefsPath,
    game::video::RendererBackend activeBackend,
    bool vsyncEnabled,
    std::unique_ptr<Window>& window,
    game::runtime::window_presentation::WindowPresentationController& presentation,
    std::ostream& logOut,
    std::ostream& errOut) {
    Result out;
    const game::video::Preferences startupVideoPrefs = game::video::loadPreferences(prefsPath);

    const auto startupDisplayBounds =
        game::runtime::video_mode::queryPrimaryDisplayUsableBounds(errOut);
    const auto startupPlacement =
        game::runtime::video_mode::resolveStartupWindowPlacement(
            startupVideoPrefs,
            startupDisplayBounds);
    const bool hasSavedWindowedSize =
        startupVideoPrefs.windowedWidth > 0 && startupVideoPrefs.windowedHeight > 0;
    presentation.applyStartupPlacement(startupPlacement);

    const auto initialWindow = game::runtime::window_bootstrap::openWindow(
        game::runtime::window_bootstrap::OpenRequest{
            game::runtime::backend_bootstrap::graphicsApiForBackend(activeBackend),
            vsyncEnabled},
        [&window, &presentation](Window::GraphicsApi graphicsApi, bool requestVsyncEnabled) {
            window = std::make_unique<Window>(
                "Pokemon Autochess",
                presentation.defaultWindowedWidth(),
                presentation.defaultWindowedHeight(),
                graphicsApi,
                requestVsyncEnabled);
        },
        [&window]() {
            return window && window->hasOpenGLContext();
        });
    if (!initialWindow.success) {
        out.error = initialWindow.error;
        return out;
    }

    presentation.bindWindow(window.get());
    presentation.setOpenGlState(initialWindow.hasOpenGlContext, initialWindow.glFunctionsReady);

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

    presentation.syncVideoModeState();
    if (startupVideoPrefs.fullscreen &&
        !presentation.applyVideoModeInternal(0, 0, true, false)) {
        errOut << "[Video] Failed to apply saved startup fullscreen mode.\n";
    }

    const auto startupOverrideResult = game::runtime::startup_video_override::apply(
        game::runtime::startup_config::readStartupVideoOverride(errOut),
        [&presentation]() { return presentation.queryVideoMode(); },
        [&presentation](int width, int height, bool isFullscreen) {
            return presentation.applyVideoModeInternal(width, height, isFullscreen, false);
        });
    if (startupOverrideResult.attempted) {
        (startupOverrideResult.applied ? logOut : errOut) << startupOverrideResult.message << "\n";
    }

    out.success = true;
    return out;
}

} // namespace game::runtime::runner_window_bootstrap
