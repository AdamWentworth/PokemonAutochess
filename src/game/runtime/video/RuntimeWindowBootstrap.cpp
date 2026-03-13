#include "game/runtime/video/RuntimeWindowBootstrap.h"

#include <exception>

namespace game::runtime::window_bootstrap {

OpenResult openWindow(const OpenRequest& request,
                      const WindowCreator& createWindow,
                      const OpenGlContextQuery& hasOpenGlContext) {
    OpenResult out;
    try {
        if (createWindow) {
            createWindow(request.graphicsApi, request.vsyncEnabled);
        }
        out.success = true;
        out.hasOpenGlContext = hasOpenGlContext ? hasOpenGlContext() : false;
        out.glFunctionsReady = false;
    } catch (const std::exception& ex) {
        out.error = ex.what();
    }
    return out;
}

} // namespace game::runtime::window_bootstrap

