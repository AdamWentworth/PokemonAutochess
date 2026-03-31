#include "game/runtime/video/RuntimeWindowPresentationController.h"

#include "engine/core/EngineServices.h"
#include "engine/platform/Window.h"
#include "engine/render/Camera3D.h"
#include "engine/render/IRenderBackend.h"
#include "game/runtime/video/VideoInitGuards.h"
#include "game/runtime/video/VideoPreferences.h"

#include <SDL2/SDL.h>
#include <glad/glad.h>

#include <algorithm>
#include <ostream>
#include <utility>

namespace game::runtime::window_presentation {

WindowPresentationController::WindowPresentationController(EngineServices& servicesIn,
                                                           std::ostream& outIn,
                                                           std::ostream& errIn)
    : services(servicesIn), out(outIn), err(errIn), log("Video", &outIn, &errIn) {}

void WindowPresentationController::setPreferencesPath(std::string prefsPathIn) {
    prefsPath = std::move(prefsPathIn);
}

void WindowPresentationController::bindWindow(Window* windowIn) {
    window = windowIn;
}

void WindowPresentationController::bindRenderer(IRenderBackend* rendererIn) {
    renderer = rendererIn;
}

void WindowPresentationController::bindCamera(Camera3D* cameraIn) {
    camera = cameraIn;
}

void WindowPresentationController::setOpenGlState(bool hasOpenGlContext, bool functionsReady) {
    windowHasOpenGLContext = hasOpenGlContext;
    glFunctionsReady = functionsReady;
}

void WindowPresentationController::setAppliedVsyncEnabled(bool enabled) {
    appliedVsyncEnabled = enabled;
}

void WindowPresentationController::applyStartupPlacement(
    const video_mode::StartupWindowPlacement& placement) {
    defaultWindowedW = placement.width;
    defaultWindowedH = placement.height;
    lastWindowedW = placement.width;
    lastWindowedH = placement.height;
    lastWindowedMaximized = placement.maximized;
}

void WindowPresentationController::updateDrawableSizeAndViewport() {
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

void WindowPresentationController::updateMouseScale() {
    if (windowW > 0 && windowH > 0) {
        mouseScaleXValue = static_cast<float>(drawableW) / static_cast<float>(windowW);
        mouseScaleYValue = static_cast<float>(drawableH) / static_cast<float>(windowH);
    } else {
        mouseScaleXValue = 1.0f;
        mouseScaleYValue = 1.0f;
    }
}

void WindowPresentationController::updateCameraAspect() {
    if (camera && drawableW > 0 && drawableH > 0) {
        camera->setAspectRatio(static_cast<float>(drawableW) / static_cast<float>(drawableH));
    }
}

void WindowPresentationController::syncVideoModeState() {
    updateDrawableSizeAndViewport();
    updateMouseScale();
    updateCameraAspect();
    if (window && window->getSDLWindow()) {
        const Uint32 flags = SDL_GetWindowFlags(window->getSDLWindow());
        fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0 ||
                     (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
        if (!fullscreen) {
            lastWindowedW = std::max(640, windowW);
            lastWindowedH = std::max(360, windowH);
            lastWindowedMaximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
            if (uncappedWindowModeNormalized &&
                !services.vsyncEnabled &&
                game::video::sanitizeFpsCap(services.fpsCap) == 0) {
                lastWindowedMaximized = false;
            }
        }
    }
    if (renderer) {
        renderer->onResize(drawableW, drawableH);
    }
}

void WindowPresentationController::noteCurrentWindowModeChanged(bool saveImmediately) {
    videoModePreferencesDirty = true;
    if (saveImmediately) {
        saveVideoModePreferences();
    }
}

void WindowPresentationController::saveVideoModePreferences() {
    if (!videoModePreferencesDirty || prefsPath.empty()) return;

    game::video::Preferences prefs = game::video::loadPreferences(prefsPath);
    const int safeWindowedW = std::max(640, lastWindowedW);
    const int safeWindowedH = std::max(360, lastWindowedH);
    if (prefs.fullscreen == fullscreen &&
        prefs.windowedWidth == safeWindowedW &&
        prefs.windowedHeight == safeWindowedH &&
        prefs.windowedMaximized == lastWindowedMaximized) {
        videoModePreferencesDirty = false;
        return;
    }

    prefs.fullscreen = fullscreen;
    prefs.windowedWidth = safeWindowedW;
    prefs.windowedHeight = safeWindowedH;
    prefs.windowedMaximized = lastWindowedMaximized;

    std::string saveErr;
    if (!game::video::savePreferences(prefs, prefsPath, &saveErr)) {
        log.error("[Video] Failed to save video mode preferences: " + saveErr);
        return;
    }
    videoModePreferencesDirty = false;
}

video_mode::RequestedVideoMode WindowPresentationController::resolveRequestedVideoMode(
    int width,
    int height,
    bool fullscreenWanted) const {
    int targetWidth = width;
    int targetHeight = height;

    if (fullscreenWanted) {
        if (targetWidth <= 0 || targetHeight <= 0) {
            SDL_DisplayMode desktopMode{};
            int displayIndex = 0;
            if (window && window->getSDLWindow()) {
                const int queriedDisplayIndex = SDL_GetWindowDisplayIndex(window->getSDLWindow());
                if (queriedDisplayIndex >= 0) {
                    displayIndex = queriedDisplayIndex;
                }
            }
            if (SDL_GetDesktopDisplayMode(displayIndex, &desktopMode) == 0 &&
                desktopMode.w > 0 &&
                desktopMode.h > 0) {
                targetWidth = desktopMode.w;
                targetHeight = desktopMode.h;
            } else {
                targetWidth = std::max(640, drawableW);
                targetHeight = std::max(360, drawableH);
            }
        }
    } else if (targetWidth <= 0 || targetHeight <= 0) {
        targetWidth = std::max(640, lastWindowedW > 0 ? lastWindowedW : defaultWindowedW);
        targetHeight = std::max(360, lastWindowedH > 0 ? lastWindowedH : defaultWindowedH);
    }

    return game::runtime::video_mode::sanitizeRequestedVideoMode(
        targetWidth,
        targetHeight,
        fullscreenWanted);
}

bool WindowPresentationController::applyVideoModeInternal(int width,
                                                          int height,
                                                          bool fullscreenWanted,
                                                          bool persistChange) {
    if (!window || !window->getSDLWindow()) return false;
    const auto requested = resolveRequestedVideoMode(width, height, fullscreenWanted);
    const auto result = game::runtime::video_mode::applyRequestedVideoMode(
        window->getSDLWindow(),
        requested,
        err);
    if (!result.success) {
        return false;
    }
    syncVideoModeState();
    fullscreen = result.fullscreen;
    if (persistChange) {
        noteCurrentWindowModeChanged(true);
    }
    return true;
}

bool WindowPresentationController::applyVideoMode(int width,
                                                  int height,
                                                  bool fullscreenWanted) {
    return applyVideoModeInternal(width, height, fullscreenWanted, true);
}

GameContext::VideoMode WindowPresentationController::queryVideoMode() const {
    const int currentWidth = fullscreen ? drawableW : windowW;
    const int currentHeight = fullscreen ? drawableH : windowH;
    return game::runtime::video_mode::makeCurrentVideoMode(
        currentWidth,
        currentHeight,
        fullscreen);
}

void WindowPresentationController::syncLivePresentationSettings() {
    if (appliedVsyncEnabled == services.vsyncEnabled) {
        return;
    }

    bool applied = false;
    if (windowHasOpenGLContext && window) {
        applied = window->setVSyncEnabled(services.vsyncEnabled);
    } else if (renderer && renderer->handlesPresentation()) {
        renderer->setVSyncEnabled(services.vsyncEnabled);
        applied = true;
    }

    if (applied) {
        appliedVsyncEnabled = services.vsyncEnabled;
        log.info(std::string("[Video] VSync live set: ") +
                 (appliedVsyncEnabled ? "On" : "Off"));
    } else {
        log.error("[Video] Failed to apply live VSync toggle.");
    }
}

void WindowPresentationController::normalizeWindowedPresentationMode() {
    if (!window || !window->getSDLWindow()) {
        uncappedWindowModeNormalized = false;
        return;
    }

    const bool shouldNormalize =
        game::runtime::video_mode::shouldPreferRestoredWindowForUncappedPresentation(
            fullscreen,
            lastWindowedMaximized,
            services.vsyncEnabled,
            services.fpsCap);
    if (!shouldNormalize) {
        uncappedWindowModeNormalized = false;
        return;
    }
    if (uncappedWindowModeNormalized) {
        return;
    }

    SDL_Window* sdlWindow = window->getSDLWindow();
    const Uint32 flags = SDL_GetWindowFlags(sdlWindow);
    if ((flags & SDL_WINDOW_MAXIMIZED) == 0u) {
        uncappedWindowModeNormalized = true;
        return;
    }

    const int restoreW = std::max(640, windowW > 0 ? windowW : lastWindowedW);
    const int restoreH = std::max(360, windowH > 0 ? windowH : lastWindowedH);
    SDL_RestoreWindow(sdlWindow);
    SDL_SetWindowSize(sdlWindow, restoreW, restoreH);
    SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    syncVideoModeState();
    lastWindowedW = restoreW;
    lastWindowedH = restoreH;
    lastWindowedMaximized = false;
    if (!prefsPath.empty()) {
        game::video::Preferences prefs = game::video::loadPreferences(prefsPath);
        prefs.fullscreen = false;
        prefs.windowedWidth = restoreW;
        prefs.windowedHeight = restoreH;
        prefs.windowedMaximized = false;
        std::string saveErr;
        if (!game::video::savePreferences(prefs, prefsPath, &saveErr)) {
            videoModePreferencesDirty = true;
            log.error("[Video] Failed to save restored windowed mode: " + saveErr);
        } else {
            videoModePreferencesDirty = false;
        }
    } else {
        noteCurrentWindowModeChanged(true);
    }
    uncappedWindowModeNormalized = true;
    log.info("[Video] Restored windowed mode for uncapped presentation.");
}

} // namespace game::runtime::window_presentation
