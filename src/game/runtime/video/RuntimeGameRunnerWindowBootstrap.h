#pragma once

#include <iosfwd>
#include <memory>
#include <string>

#include "game/runtime/video/VideoPreferences.h"

class Window;

namespace game::runtime::window_presentation {
class WindowPresentationController;
}

namespace game::runtime::runner_window_bootstrap {

struct Result {
    bool success = false;
    std::string error;
};

Result openAndApplyStartupWindow(
    const std::string& prefsPath,
    game::video::RendererBackend activeBackend,
    bool vsyncEnabled,
    std::unique_ptr<Window>& window,
    game::runtime::window_presentation::WindowPresentationController& presentation,
    std::ostream& logOut,
    std::ostream& errOut);

} // namespace game::runtime::runner_window_bootstrap
