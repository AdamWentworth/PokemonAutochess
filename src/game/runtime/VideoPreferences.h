#pragma once

#include <string>
#include <string_view>

namespace game::video {

enum class RendererBackend {
    Auto,
    OpenGL,
    Vulkan,
    D3D12
};

struct Preferences {
    std::string rendererBackend = "opengl";
    bool requireDiscreteGpu = false;
    std::string preferredGpuAdapter;
    bool characterInking = false;
    bool restartOnExit = false;
    std::string bootMenuScreen;
};

std::string defaultPreferencesPath();

RendererBackend parseRendererBackend(std::string_view token);
bool isKnownRendererBackendToken(std::string_view token);
const char* rendererBackendName(RendererBackend backend);
bool isRendererBackendImplemented(RendererBackend backend);

Preferences loadPreferences(const std::string& path = defaultPreferencesPath());
bool savePreferences(const Preferences& prefs,
                     const std::string& path = defaultPreferencesPath(),
                     std::string* outError = nullptr);

} // namespace game::video
