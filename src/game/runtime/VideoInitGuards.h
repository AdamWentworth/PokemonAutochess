#pragma once

namespace game::runtime::video {

inline bool shouldApplyOpenGLViewport(bool hasOpenGLContext, bool glFunctionsReady) {
    return hasOpenGLContext && glFunctionsReady;
}

} // namespace game::runtime::video
