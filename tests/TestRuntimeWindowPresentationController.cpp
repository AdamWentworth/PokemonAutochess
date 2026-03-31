#include <sstream>
#include <string>

#include "engine/core/EngineServices.h"
#include "game/runtime/video/RuntimeWindowPresentationController.h"

bool test_runtime_window_presentation_controller_contract(std::string& outFail) {
    namespace window_presentation = game::runtime::window_presentation;

    EngineServices services;
    std::ostringstream out;
    std::ostringstream err;
    window_presentation::WindowPresentationController controller(services, out, err);

    {
        const auto mode = controller.queryVideoMode();
        if (mode.width != 1280 || mode.height != 720 || mode.fullscreen) {
            outFail = "WindowPresentationController should start with the default windowed video mode.";
            return false;
        }
    }

    {
        game::runtime::video_mode::StartupWindowPlacement placement;
        placement.width = 1600;
        placement.height = 900;
        placement.maximized = true;
        controller.applyStartupPlacement(placement);

        if (controller.defaultWindowedWidth() != 1600 ||
            controller.defaultWindowedHeight() != 900) {
            outFail = "applyStartupPlacement should seed the default windowed size used by the controller.";
            return false;
        }

        const auto requested = controller.resolveRequestedVideoMode(0, 0, false);
        if (requested.width != 1600 || requested.height != 900 || requested.fullscreen) {
            outFail = "Windowed fallback requests should use the remembered startup placement size.";
            return false;
        }
    }

    {
        const auto requested = controller.resolveRequestedVideoMode(320, 200, false);
        if (requested.width != 640 || requested.height != 360 || requested.fullscreen) {
            outFail = "Windowed requests should still flow through the shared minimum video-mode sanitizer.";
            return false;
        }
    }

    {
        const auto requested = controller.resolveRequestedVideoMode(320, 200, true);
        if (requested.width != 640 || requested.height != 360 || !requested.fullscreen) {
            outFail = "Explicit fullscreen requests should preserve fullscreen intent while clamping size.";
            return false;
        }
    }

    return true;
}
