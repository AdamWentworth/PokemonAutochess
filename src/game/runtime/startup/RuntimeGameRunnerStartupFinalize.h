#pragma once

#include <functional>
#include <iosfwd>
#include <memory>
#include <string>

class Camera3D;
class IRenderBackend;
struct EngineServices;

namespace game::runtime::window_presentation {
class WindowPresentationController;
}

namespace game::runtime::runner_startup_finalize {

struct Result {
    bool success = false;
    std::string error;
};

Result activateRendererAndInitializePresentation(
    IRenderBackend& renderer,
    EngineServices& services,
    game::runtime::window_presentation::WindowPresentationController& presentation,
    std::unique_ptr<Camera3D>& camera,
    const std::function<void(float)>& renderBootLoading,
    std::ostream& logOut,
    std::ostream& errOut);

} // namespace game::runtime::runner_startup_finalize
