#include "game/runtime/RuntimeSdlVideoMode.h"

#include <algorithm>
#include <ostream>

namespace game::runtime::video_mode {

RequestedVideoMode sanitizeRequestedVideoMode(int width, int height, bool fullscreen) {
    RequestedVideoMode out;
    out.width = std::max(640, width);
    out.height = std::max(360, height);
    out.fullscreen = fullscreen;
    return out;
}

ApplyVideoModeResult applyRequestedVideoMode(SDL_Window* window,
                                             const RequestedVideoMode& requested,
                                             std::ostream& err,
                                             const SdlApi& api) {
    ApplyVideoModeResult out;
    if (window == nullptr) {
        return out;
    }

    if (requested.fullscreen) {
        SDL_DisplayMode mode{};
        mode.w = requested.width;
        mode.h = requested.height;
        mode.format = SDL_PIXELFORMAT_UNKNOWN;
        mode.refresh_rate = 0;
        mode.driverdata = nullptr;
        if (api.setWindowDisplayMode(window, &mode) != 0) {
            err << "[Video] SDL_SetWindowDisplayMode failed: " << api.getError() << "\n";
        }
        if (api.setWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) != 0) {
            err << "[Video] SDL_WINDOW_FULLSCREEN failed, trying desktop: " << api.getError() << "\n";
            if (api.setWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
                err << "[Video] SDL_WINDOW_FULLSCREEN_DESKTOP failed: " << api.getError() << "\n";
                return out;
            }
        }
        out.success = true;
        out.fullscreen = true;
        return out;
    }

    if (api.setWindowFullscreen(window, 0) != 0) {
        err << "[Video] Exiting fullscreen failed: " << api.getError() << "\n";
        return out;
    }
    api.setWindowSize(window, requested.width, requested.height);
    api.setWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    out.success = true;
    out.fullscreen = false;
    return out;
}

GameContext::VideoMode makeCurrentVideoMode(int drawableWidth,
                                            int drawableHeight,
                                            bool fullscreen) {
    GameContext::VideoMode out;
    out.width = std::max(1, drawableWidth);
    out.height = std::max(1, drawableHeight);
    out.fullscreen = fullscreen;
    return out;
}

} // namespace game::runtime::video_mode
