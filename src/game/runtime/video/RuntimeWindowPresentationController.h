#pragma once

#include "engine/utils/LogSink.h"

#include <iosfwd>
#include <string>

#include "engine/core/GameContext.h"
#include "game/runtime/video/RuntimeSdlVideoMode.h"

class Camera3D;
struct EngineServices;
class IRenderBackend;
class Window;

namespace game::runtime::window_presentation {

class WindowPresentationController {
public:
    WindowPresentationController(EngineServices& services,
                                 std::ostream& out,
                                 std::ostream& err);

    void setPreferencesPath(std::string prefsPath);
    void bindWindow(Window* window);
    void bindRenderer(IRenderBackend* renderer);
    void bindCamera(Camera3D* camera);
    void setOpenGlState(bool hasOpenGlContext, bool functionsReady);
    void setAppliedVsyncEnabled(bool enabled);
    void applyStartupPlacement(const video_mode::StartupWindowPlacement& placement);

    void syncVideoModeState();
    void noteCurrentWindowModeChanged(bool saveImmediately);
    void saveVideoModePreferences();
    video_mode::RequestedVideoMode resolveRequestedVideoMode(int width,
                                                            int height,
                                                            bool fullscreenWanted) const;
    bool applyVideoModeInternal(int width,
                                int height,
                                bool fullscreenWanted,
                                bool persistChange);
    bool applyVideoMode(int width, int height, bool fullscreenWanted);
    GameContext::VideoMode queryVideoMode() const;
    void syncLivePresentationSettings();
    void normalizeWindowedPresentationMode();

    int drawableWidth() const { return drawableW; }
    int drawableHeight() const { return drawableH; }
    int windowWidth() const { return windowW; }
    int windowHeight() const { return windowH; }
    int defaultWindowedWidth() const { return defaultWindowedW; }
    int defaultWindowedHeight() const { return defaultWindowedH; }
    float mouseScaleX() const { return mouseScaleXValue; }
    float mouseScaleY() const { return mouseScaleYValue; }
    bool hasOpenGlContext() const { return windowHasOpenGLContext; }
    bool openGlFunctionsReady() const { return glFunctionsReady; }

private:
    void updateDrawableSizeAndViewport();
    void updateMouseScale();
    void updateCameraAspect();

private:
    EngineServices& services;
    std::ostream& out;
    std::ostream& err;
    engine::log::Sink log;
    Window* window = nullptr;
    IRenderBackend* renderer = nullptr;
    Camera3D* camera = nullptr;
    std::string prefsPath;

    int drawableW = 1280;
    int drawableH = 720;
    int windowW = 1280;
    int windowH = 720;
    int defaultWindowedW = 1280;
    int defaultWindowedH = 720;
    int lastWindowedW = 1280;
    int lastWindowedH = 720;
    bool fullscreen = false;
    bool lastWindowedMaximized = false;
    bool videoModePreferencesDirty = false;
    bool windowHasOpenGLContext = false;
    bool glFunctionsReady = false;
    bool appliedVsyncEnabled = false;
    bool uncappedWindowModeNormalized = false;
    float mouseScaleXValue = 1.0f;
    float mouseScaleYValue = 1.0f;
};

} // namespace game::runtime::window_presentation
