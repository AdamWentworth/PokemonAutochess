#pragma once

#include <functional>
#include <memory>
#include <string>

#include "game/runtime/video/VideoPreferences.h"

class Window;
struct EngineServices;

namespace game::runtime::renderer_recovery {
struct Result;
}

namespace game::runtime::window_presentation {
class WindowPresentationController;
}

namespace game::runtime::runner_renderer_bootstrap {

game::runtime::renderer_recovery::Result createWithOpenGlFallback(
    game::video::RendererBackend activeBackend,
    const std::string& activeBackendName,
    EngineServices& services,
    std::unique_ptr<Window>& window,
    game::runtime::window_presentation::WindowPresentationController& presentation,
    const std::function<bool(std::string*)>& loadOpenGlFunctions,
    int fallbackWindowWidth,
    int fallbackWindowHeight);

} // namespace game::runtime::runner_renderer_bootstrap
