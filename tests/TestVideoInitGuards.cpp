#include "game/runtime/video/VideoInitGuards.h"

#include <string>

bool test_video_init_gl_viewport_guard(std::string& outFail) {
    using game::runtime::video::shouldApplyOpenGLViewport;

    if (shouldApplyOpenGLViewport(true, false)) {
        outFail = "OpenGL viewport should not be applied before GL functions are ready";
        return false;
    }
    if (shouldApplyOpenGLViewport(false, true)) {
        outFail = "OpenGL viewport should not be applied without an OpenGL context";
        return false;
    }
    if (shouldApplyOpenGLViewport(false, false)) {
        outFail = "OpenGL viewport guard should be false when neither condition is true";
        return false;
    }
    if (!shouldApplyOpenGLViewport(true, true)) {
        outFail = "OpenGL viewport guard should be true only when context and GL readiness are true";
        return false;
    }
    return true;
}

