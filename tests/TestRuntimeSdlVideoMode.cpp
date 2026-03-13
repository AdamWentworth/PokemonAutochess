#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <SDL2/SDL.h>

#include "game/runtime/video/RuntimeSdlVideoMode.h"

namespace {

struct FakeSdlState {
    int setWindowDisplayModeResult = 0;
    std::vector<int> setWindowFullscreenResults;
    std::string errorText = "fake sdl error";

    int setWindowDisplayModeCalls = 0;
    std::vector<Uint32> fullscreenFlags;
    std::vector<std::pair<int, int>> sizes;
    std::vector<std::pair<int, int>> positions;
    SDL_DisplayMode lastDisplayMode{};
};

FakeSdlState* g_fakeSdlState = nullptr;

int fakeSetWindowDisplayMode(SDL_Window*, const SDL_DisplayMode* mode) {
    if (g_fakeSdlState == nullptr) return -1;
    ++g_fakeSdlState->setWindowDisplayModeCalls;
    if (mode != nullptr) {
        g_fakeSdlState->lastDisplayMode = *mode;
    }
    return g_fakeSdlState->setWindowDisplayModeResult;
}

int fakeSetWindowFullscreen(SDL_Window*, Uint32 flags) {
    if (g_fakeSdlState == nullptr) return -1;
    g_fakeSdlState->fullscreenFlags.push_back(flags);
    const std::size_t index = g_fakeSdlState->fullscreenFlags.size() - 1;
    if (index < g_fakeSdlState->setWindowFullscreenResults.size()) {
        return g_fakeSdlState->setWindowFullscreenResults[index];
    }
    return 0;
}

void fakeSetWindowSize(SDL_Window*, int width, int height) {
    if (g_fakeSdlState == nullptr) return;
    g_fakeSdlState->sizes.emplace_back(width, height);
}

void fakeSetWindowPosition(SDL_Window*, int x, int y) {
    if (g_fakeSdlState == nullptr) return;
    g_fakeSdlState->positions.emplace_back(x, y);
}

const char* fakeGetError() {
    return g_fakeSdlState == nullptr ? "missing fake sdl state" : g_fakeSdlState->errorText.c_str();
}

game::runtime::video_mode::SdlApi makeFakeSdlApi() {
    game::runtime::video_mode::SdlApi api;
    api.setWindowDisplayMode = &fakeSetWindowDisplayMode;
    api.setWindowFullscreen = &fakeSetWindowFullscreen;
    api.setWindowSize = &fakeSetWindowSize;
    api.setWindowPosition = &fakeSetWindowPosition;
    api.getError = &fakeGetError;
    return api;
}

} // namespace

bool test_runtime_sdl_video_mode_contract(std::string& outFail) {
    using game::runtime::video_mode::applyRequestedVideoMode;
    using game::runtime::video_mode::makeCurrentVideoMode;
    using game::runtime::video_mode::sanitizeRequestedVideoMode;

    {
        const auto requested = sanitizeRequestedVideoMode(320, 200, true);
        if (requested.width != 640 || requested.height != 360 || !requested.fullscreen) {
            outFail = "sanitizeRequestedVideoMode should clamp to the minimum supported resolution.";
            return false;
        }
    }

    {
        const auto mode = makeCurrentVideoMode(0, -2, true);
        if (mode.width != 1 || mode.height != 1 || !mode.fullscreen) {
            outFail = "makeCurrentVideoMode should clamp drawable dimensions to positive values.";
            return false;
        }
    }

    {
        FakeSdlState state;
        state.setWindowFullscreenResults = {0};
        g_fakeSdlState = &state;

        std::ostringstream errs;
        const auto result = applyRequestedVideoMode(
            reinterpret_cast<SDL_Window*>(1),
            sanitizeRequestedVideoMode(800, 600, true),
            errs,
            makeFakeSdlApi());
        g_fakeSdlState = nullptr;

        if (!result.success || !result.fullscreen ||
            state.setWindowDisplayModeCalls != 1 ||
            state.lastDisplayMode.w != 800 ||
            state.lastDisplayMode.h != 600 ||
            state.fullscreenFlags.size() != 1 ||
            state.fullscreenFlags[0] != SDL_WINDOW_FULLSCREEN ||
            !errs.str().empty()) {
            outFail = "applyRequestedVideoMode should apply exclusive fullscreen when SDL accepts it.";
            return false;
        }
    }

    {
        FakeSdlState state;
        state.setWindowDisplayModeResult = -1;
        state.setWindowFullscreenResults = {0};
        state.errorText = "display mode unsupported";
        g_fakeSdlState = &state;

        std::ostringstream errs;
        const auto result = applyRequestedVideoMode(
            reinterpret_cast<SDL_Window*>(1),
            sanitizeRequestedVideoMode(1280, 720, true),
            errs,
            makeFakeSdlApi());
        g_fakeSdlState = nullptr;

        if (!result.success || !result.fullscreen ||
            errs.str().find("SDL_SetWindowDisplayMode failed") == std::string::npos) {
            outFail = "applyRequestedVideoMode should warn but continue when display mode selection fails.";
            return false;
        }
    }

    {
        FakeSdlState state;
        state.setWindowFullscreenResults = {-1, 0};
        state.errorText = "exclusive fullscreen unavailable";
        g_fakeSdlState = &state;

        std::ostringstream errs;
        const auto result = applyRequestedVideoMode(
            reinterpret_cast<SDL_Window*>(1),
            sanitizeRequestedVideoMode(1280, 720, true),
            errs,
            makeFakeSdlApi());
        g_fakeSdlState = nullptr;

        if (!result.success || !result.fullscreen ||
            state.fullscreenFlags.size() != 2 ||
            state.fullscreenFlags[0] != SDL_WINDOW_FULLSCREEN ||
            state.fullscreenFlags[1] != SDL_WINDOW_FULLSCREEN_DESKTOP ||
            errs.str().find("trying desktop") == std::string::npos) {
            outFail = "applyRequestedVideoMode should fall back from exclusive to desktop fullscreen.";
            return false;
        }
    }

    {
        FakeSdlState state;
        state.setWindowFullscreenResults = {-1, -1};
        state.errorText = "fullscreen rejected";
        g_fakeSdlState = &state;

        std::ostringstream errs;
        const auto result = applyRequestedVideoMode(
            reinterpret_cast<SDL_Window*>(1),
            sanitizeRequestedVideoMode(1280, 720, true),
            errs,
            makeFakeSdlApi());
        g_fakeSdlState = nullptr;

        if (result.success || result.fullscreen ||
            errs.str().find("SDL_WINDOW_FULLSCREEN_DESKTOP failed") == std::string::npos) {
            outFail = "applyRequestedVideoMode should fail when both fullscreen modes are rejected.";
            return false;
        }
    }

    {
        FakeSdlState state;
        state.setWindowFullscreenResults = {0};
        g_fakeSdlState = &state;

        std::ostringstream errs;
        const auto result = applyRequestedVideoMode(
            reinterpret_cast<SDL_Window*>(1),
            sanitizeRequestedVideoMode(1024, 576, false),
            errs,
            makeFakeSdlApi());
        g_fakeSdlState = nullptr;

        if (!result.success || result.fullscreen ||
            state.fullscreenFlags.size() != 1 ||
            state.fullscreenFlags[0] != 0 ||
            state.sizes.size() != 1 ||
            state.sizes[0] != std::make_pair(1024, 576) ||
            state.positions.size() != 1 ||
            state.positions[0] != std::make_pair(SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED) ||
            !errs.str().empty()) {
            outFail = "applyRequestedVideoMode should resize and center the windowed mode.";
            return false;
        }
    }

    {
        FakeSdlState state;
        state.setWindowFullscreenResults = {-1};
        state.errorText = "could not exit fullscreen";
        g_fakeSdlState = &state;

        std::ostringstream errs;
        const auto result = applyRequestedVideoMode(
            reinterpret_cast<SDL_Window*>(1),
            sanitizeRequestedVideoMode(1024, 576, false),
            errs,
            makeFakeSdlApi());
        g_fakeSdlState = nullptr;

        if (result.success || result.fullscreen ||
            !state.sizes.empty() ||
            !state.positions.empty() ||
            errs.str().find("Exiting fullscreen failed") == std::string::npos) {
            outFail = "applyRequestedVideoMode should stop before resizing when exiting fullscreen fails.";
            return false;
        }
    }

    return true;
}

