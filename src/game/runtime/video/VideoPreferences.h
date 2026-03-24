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

enum class GraphicsQuality {
    Low = 0,
    Medium = 1,
    High = 2,
    Ultra = 3,
};

struct Preferences {
    std::string rendererBackend = "opengl";
    bool vsync = false;
    int fpsCap = 0;
    int graphicsQuality = static_cast<int>(GraphicsQuality::Ultra);
    bool requireDiscreteGpu = false;
    std::string preferredGpuAdapter;
    bool characterInking = false;
    int audioMasterVolume = 100;
    int audioMusicVolume = 100;
    int audioSfxVolume = 100;
    int audioVoiceVolume = 100;
    bool audioMute = false;
    bool fullscreen = false;
    int windowedWidth = 0;
    int windowedHeight = 0;
    bool windowedMaximized = false;
    bool restartOnExit = false;
    std::string bootMenuScreen;
};

std::string defaultPreferencesPath();

RendererBackend parseRendererBackend(std::string_view token);
bool isKnownRendererBackendToken(std::string_view token);
const char* rendererBackendName(RendererBackend backend);
bool isRendererBackendImplemented(RendererBackend backend);
GraphicsQuality parseGraphicsQuality(std::string_view token);
bool isKnownGraphicsQualityToken(std::string_view token);
const char* graphicsQualityName(GraphicsQuality quality);
int sanitizeGraphicsQuality(int quality);
int sanitizeFpsCap(int fpsCap);
int sanitizeVolumePercent(int volumePercent);

Preferences loadPreferences(const std::string& path = defaultPreferencesPath());
bool savePreferences(const Preferences& prefs,
                     const std::string& path = defaultPreferencesPath(),
                     std::string* outError = nullptr);

} // namespace game::video
