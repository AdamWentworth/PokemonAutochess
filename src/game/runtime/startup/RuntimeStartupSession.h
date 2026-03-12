#pragma once

#include "engine/core/EngineServices.h"
#include "game/runtime/VideoPreferences.h"

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace game::runtime::startup_session {

struct PreparedSession {
    game::video::RendererBackend requestedBackend = game::video::RendererBackend::Auto;
    game::video::RendererBackend activeBackend = game::video::RendererBackend::OpenGL;
    std::string requestedBackendName = "opengl";
    std::string bootMenuScreen;
    bool rendererBackendFallback = false;
    std::string rendererBackendFallbackReason;
    bool vsyncEnabled = false;
    bool requireDiscreteGpu = false;
    std::string preferredGpuAdapter;
    bool characterInkingEnabled = false;
    std::vector<std::string> availableGpuAdapters;
};

PreparedSession prepare(const std::string& prefsPath,
                        const std::optional<std::string>& envBackend,
                        std::ostream& logOut,
                        std::ostream& errOut);

PreparedSession prepareFromEnvironment(const std::string& prefsPath,
                                       std::ostream& logOut,
                                       std::ostream& errOut);

void applyToServices(const PreparedSession& session, EngineServices& services);

} // namespace game::runtime::startup_session
