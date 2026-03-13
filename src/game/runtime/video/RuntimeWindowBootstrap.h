#pragma once

#include "engine/platform/Window.h"

#include <functional>
#include <string>

namespace game::runtime::window_bootstrap {

struct OpenRequest {
    Window::GraphicsApi graphicsApi = Window::GraphicsApi::OpenGL;
    bool vsyncEnabled = false;
};

struct OpenResult {
    bool success = false;
    bool hasOpenGlContext = false;
    bool glFunctionsReady = false;
    std::string error;
};

using WindowCreator = std::function<void(Window::GraphicsApi, bool)>;
using OpenGlContextQuery = std::function<bool()>;

OpenResult openWindow(const OpenRequest& request,
                      const WindowCreator& createWindow,
                      const OpenGlContextQuery& hasOpenGlContext);

} // namespace game::runtime::window_bootstrap
