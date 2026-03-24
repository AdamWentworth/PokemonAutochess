#pragma once

#include "engine/core/EngineServices.h"
#include "game/runtime/video/VideoPreferences.h"

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
    int fpsCap = 0;
    int graphicsQuality = static_cast<int>(game::video::GraphicsQuality::Ultra);
    bool requireDiscreteGpu = false;
    std::string preferredGpuAdapter;
    bool characterInkingEnabled = false;
    int audioMasterVolume = 100;
    int audioMusicVolume = 100;
    int audioSfxVolume = 100;
    int audioVoiceVolume = 100;
    bool audioMute = false;
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

