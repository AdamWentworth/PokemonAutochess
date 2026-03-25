#pragma once

#include <iosfwd>

#include <SDL2/SDL.h>

#include "engine/core/GameContext.h"
#include "game/runtime/video/VideoPreferences.h"

namespace game::runtime::video_mode {

struct RequestedVideoMode {
    int width = 640;
    int height = 360;
    bool fullscreen = false;
};

struct ApplyVideoModeResult {
    bool success = false;
    bool fullscreen = false;
};

struct DisplayUsableBounds {
    int x = 0;
    int y = 0;
    int width = 1280;
    int height = 720;
    bool valid = false;
};

struct StartupWindowPlacement {
    int x = SDL_WINDOWPOS_CENTERED;
    int y = SDL_WINDOWPOS_CENTERED;
    int width = 1280;
    int height = 720;
    bool maximized = false;
};

struct SdlApi {
    int (*setWindowDisplayMode)(SDL_Window*, const SDL_DisplayMode*) = SDL_SetWindowDisplayMode;
    int (*setWindowFullscreen)(SDL_Window*, Uint32) = SDL_SetWindowFullscreen;
    void (*setWindowSize)(SDL_Window*, int, int) = SDL_SetWindowSize;
    void (*setWindowPosition)(SDL_Window*, int, int) = SDL_SetWindowPosition;
    int (*getDisplayUsableBounds)(int, SDL_Rect*) = SDL_GetDisplayUsableBounds;
    const char* (*getError)() = SDL_GetError;
};

RequestedVideoMode sanitizeRequestedVideoMode(int width, int height, bool fullscreen);
DisplayUsableBounds queryPrimaryDisplayUsableBounds(std::ostream& err, const SdlApi& api = {});
StartupWindowPlacement resolveStartupWindowPlacement(const game::video::Preferences& prefs,
                                                     const DisplayUsableBounds& displayBounds);
bool shouldPreferRestoredWindowForUncappedPresentation(bool fullscreen,
                                                       bool maximized,
                                                       bool vsyncEnabled,
                                                       int fpsCap);

ApplyVideoModeResult applyRequestedVideoMode(SDL_Window* window,
                                             const RequestedVideoMode& requested,
                                             std::ostream& err,
                                             const SdlApi& api = {});

GameContext::VideoMode makeCurrentVideoMode(int drawableWidth,
                                            int drawableHeight,
                                            bool fullscreen);

} // namespace game::runtime::video_mode
