#include "game/runtime/video/RuntimeSdlVideoMode.h"

#include <algorithm>
#include <ostream>

namespace game::runtime::video_mode {
namespace {

constexpr int kDefaultWindowWidth = 1280;
constexpr int kDefaultWindowHeight = 720;

int clampWindowWidth(int width, int maxWidth) {
    if (maxWidth > 0) {
        return std::clamp(width, 640, maxWidth);
    }
    return std::max(640, width);
}

int clampWindowHeight(int height, int maxHeight) {
    if (maxHeight > 0) {
        return std::clamp(height, 360, maxHeight);
    }
    return std::max(360, height);
}

} // namespace

RequestedVideoMode sanitizeRequestedVideoMode(int width, int height, bool fullscreen) {
    RequestedVideoMode out;
    out.width = std::max(640, width);
    out.height = std::max(360, height);
    out.fullscreen = fullscreen;
    return out;
}

DisplayUsableBounds queryPrimaryDisplayUsableBounds(std::ostream& err, const SdlApi& api) {
    DisplayUsableBounds out;
    SDL_Rect usableBounds{};
    if (api.getDisplayUsableBounds != nullptr &&
        api.getDisplayUsableBounds(0, &usableBounds) == 0 &&
        usableBounds.w > 0 &&
        usableBounds.h > 0) {
        out.x = usableBounds.x;
        out.y = usableBounds.y;
        out.width = std::max(640, usableBounds.w);
        out.height = std::max(360, usableBounds.h);
        out.valid = true;
        return out;
    }

    err << "[Video] SDL_GetDisplayUsableBounds failed: " << api.getError() << "\n";
    out.width = kDefaultWindowWidth;
    out.height = kDefaultWindowHeight;
    return out;
}

StartupWindowPlacement resolveStartupWindowPlacement(const game::video::Preferences& prefs,
                                                     const DisplayUsableBounds& displayBounds) {
    StartupWindowPlacement out;
    const int maxWidth = displayBounds.valid ? displayBounds.width : 0;
    const int maxHeight = displayBounds.valid ? displayBounds.height : 0;
    const bool hasSavedWindowedSize = prefs.windowedWidth > 0 && prefs.windowedHeight > 0;
    const int defaultWidth = displayBounds.valid ? displayBounds.width : kDefaultWindowWidth;
    const int defaultHeight = displayBounds.valid ? displayBounds.height : kDefaultWindowHeight;

    out.width = hasSavedWindowedSize
                    ? clampWindowWidth(prefs.windowedWidth, maxWidth)
                    : defaultWidth;
    out.height = hasSavedWindowedSize
                     ? clampWindowHeight(prefs.windowedHeight, maxHeight)
                     : defaultHeight;
    out.maximized = prefs.windowedMaximized || !hasSavedWindowedSize;
    if (displayBounds.valid) {
        out.x = displayBounds.x;
        out.y = displayBounds.y;
    }
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

