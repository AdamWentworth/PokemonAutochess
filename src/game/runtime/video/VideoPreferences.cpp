#include "game/runtime/video/VideoPreferences.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "engine/core/Paths.h"

namespace game::video {
namespace {

std::string normalizeToken(std::string token) {
    token.erase(std::remove_if(token.begin(), token.end(),
                               [](unsigned char c) { return std::isspace(c) != 0; }),
                token.end());
    for (char& c : token) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '-') c = '_';
    }
    return token;
}

std::string normalizeMenuScreenToken(std::string token) {
    token = normalizeToken(std::move(token));
    if (token == "display") {
        return "video";
    }
    if (token == "video" ||
        token == "graphics" ||
        token == "advanced" ||
        token == "settings" ||
        token == "audio" ||
        token == "controls" ||
        token == "gameplay" ||
        token == "accessibility" ||
        token == "main") {
        return token;
    }
    return {};
}

} // namespace

std::string defaultPreferencesPath() {
    return engine::paths::data("config/user/video_settings.json");
}

RendererBackend parseRendererBackend(std::string_view tokenView) {
    const std::string token = normalizeToken(std::string(tokenView));
    if (token.empty() || token == "auto") return RendererBackend::Auto;
    if (token == "opengl_shared" || token == "gl_shared" ||
        token == "opengl_contracts" || token == "gl_contracts") {
        // Backward-compat alias from the previous dual OpenGL token model.
        return RendererBackend::OpenGL;
    }
    if (token == "opengl" || token == "gl") return RendererBackend::OpenGL;
    if (token == "vulkan" || token == "vk") return RendererBackend::Vulkan;
    if (token == "d3d12" || token == "dx12" || token == "direct3d12") return RendererBackend::D3D12;
    return RendererBackend::Auto;
}

GraphicsQuality parseGraphicsQuality(std::string_view tokenView) {
    const std::string token = normalizeToken(std::string(tokenView));
    if (token.empty() || token == "ultra" || token == "max" || token == "maximum") {
        return GraphicsQuality::Ultra;
    }
    if (token == "high") return GraphicsQuality::High;
    if (token == "medium" || token == "med") return GraphicsQuality::Medium;
    if (token == "low") return GraphicsQuality::Low;
    return GraphicsQuality::Ultra;
}

bool isKnownRendererBackendToken(std::string_view tokenView) {
    const std::string token = normalizeToken(std::string(tokenView));
    if (token.empty()) return true;
    return token == "auto" || token == "opengl" || token == "gl" ||
           token == "opengl_shared" || token == "gl_shared" ||
           token == "opengl_contracts" || token == "gl_contracts" ||
           token == "vulkan" || token == "vk" ||
           token == "d3d12" || token == "dx12" || token == "direct3d12";
}

bool isKnownGraphicsQualityToken(std::string_view tokenView) {
    const std::string token = normalizeToken(std::string(tokenView));
    if (token.empty()) return true;
    return token == "low" ||
           token == "medium" ||
           token == "med" ||
           token == "high" ||
           token == "ultra" ||
           token == "max" ||
           token == "maximum";
}

const char* rendererBackendName(RendererBackend backend) {
    switch (backend) {
    case RendererBackend::Auto:
        return "auto";
    case RendererBackend::OpenGL:
        return "opengl";
    case RendererBackend::Vulkan:
        return "vulkan";
    case RendererBackend::D3D12:
        return "d3d12";
    default:
        return "auto";
    }
}

const char* graphicsQualityName(GraphicsQuality quality) {
    switch (quality) {
    case GraphicsQuality::Low:
        return "low";
    case GraphicsQuality::Medium:
        return "medium";
    case GraphicsQuality::High:
        return "high";
    case GraphicsQuality::Ultra:
        return "ultra";
    default:
        return "ultra";
    }
}

bool isRendererBackendImplemented(RendererBackend backend) {
    if (backend == RendererBackend::Auto ||
        backend == RendererBackend::OpenGL ||
        backend == RendererBackend::Vulkan) {
        return true;
    }
#if defined(_WIN32)
    if (backend == RendererBackend::D3D12) {
        return true;
    }
#endif
    return false;
}

int sanitizeGraphicsQuality(int quality) {
    return std::clamp(
        quality,
        static_cast<int>(GraphicsQuality::Low),
        static_cast<int>(GraphicsQuality::Ultra));
}

int sanitizeFpsCap(int fpsCap) {
    if (fpsCap <= 0) return 0;
    return std::min(fpsCap, 1000);
}

int sanitizeVolumePercent(int volumePercent) {
    return std::clamp(volumePercent, 0, 100);
}

Preferences loadPreferences(const std::string& path) {
    Preferences out;

    std::ifstream in(path);
    if (!in.is_open()) return out;

    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        return out;
    }

    if (!j.is_object()) return out;

    const std::string backend = j.value("renderer_backend", out.rendererBackend);
    if (isKnownRendererBackendToken(backend)) {
        out.rendererBackend = rendererBackendName(parseRendererBackend(backend));
    }
    out.vsync = j.value("vsync", out.vsync);
    out.fpsCap = sanitizeFpsCap(j.value("fps_cap", out.fpsCap));
    if (const auto it = j.find("graphics_quality"); it != j.end()) {
        if (it->is_string()) {
            const std::string qualityToken = it->get<std::string>();
            if (isKnownGraphicsQualityToken(qualityToken)) {
                out.graphicsQuality = static_cast<int>(
                    parseGraphicsQuality(qualityToken));
            }
        } else if (it->is_number_integer()) {
            out.graphicsQuality = sanitizeGraphicsQuality(it->get<int>());
        }
    }
    out.requireDiscreteGpu = j.value("require_discrete_gpu", out.requireDiscreteGpu);
    out.preferredGpuAdapter = j.value("preferred_gpu_adapter", out.preferredGpuAdapter);
    out.characterInking = j.value("character_inking", out.characterInking);
    out.audioMasterVolume = sanitizeVolumePercent(
        j.value("audio_master_volume", out.audioMasterVolume));
    out.audioMusicVolume = sanitizeVolumePercent(
        j.value("audio_music_volume", out.audioMusicVolume));
    out.audioSfxVolume = sanitizeVolumePercent(
        j.value("audio_sfx_volume", out.audioSfxVolume));
    out.audioVoiceVolume = sanitizeVolumePercent(
        j.value("audio_voice_volume", out.audioVoiceVolume));
    out.audioMute = j.value("audio_mute", out.audioMute);
    out.fullscreen = j.value("fullscreen", out.fullscreen);
    out.windowedWidth = std::max(0, j.value("windowed_width", out.windowedWidth));
    out.windowedHeight = std::max(0, j.value("windowed_height", out.windowedHeight));
    out.windowedMaximized = j.value("windowed_maximized", out.windowedMaximized);
    out.restartOnExit = j.value("restart_on_exit", out.restartOnExit);

    const std::string menuScreen = j.value("boot_menu_screen", std::string());
    out.bootMenuScreen = normalizeMenuScreenToken(menuScreen);
    return out;
}

bool savePreferences(const Preferences& prefs, const std::string& path, std::string* outError) {
    nlohmann::json j = nlohmann::json::object();
    j["renderer_backend"] = rendererBackendName(parseRendererBackend(prefs.rendererBackend));
    j["vsync"] = prefs.vsync;
    j["fps_cap"] = sanitizeFpsCap(prefs.fpsCap);
    j["graphics_quality"] = graphicsQualityName(static_cast<GraphicsQuality>(
        sanitizeGraphicsQuality(prefs.graphicsQuality)));
    j["require_discrete_gpu"] = prefs.requireDiscreteGpu;
    j["preferred_gpu_adapter"] = prefs.preferredGpuAdapter;
    j["character_inking"] = prefs.characterInking;
    j["audio_master_volume"] = sanitizeVolumePercent(prefs.audioMasterVolume);
    j["audio_music_volume"] = sanitizeVolumePercent(prefs.audioMusicVolume);
    j["audio_sfx_volume"] = sanitizeVolumePercent(prefs.audioSfxVolume);
    j["audio_voice_volume"] = sanitizeVolumePercent(prefs.audioVoiceVolume);
    j["audio_mute"] = prefs.audioMute;
    j["fullscreen"] = prefs.fullscreen;
    j["windowed_width"] = std::max(0, prefs.windowedWidth);
    j["windowed_height"] = std::max(0, prefs.windowedHeight);
    j["windowed_maximized"] = prefs.windowedMaximized;
    j["restart_on_exit"] = prefs.restartOnExit;
    j["boot_menu_screen"] = normalizeMenuScreenToken(prefs.bootMenuScreen);

    std::error_code ec;
    const std::filesystem::path outPath(path);
    std::filesystem::create_directories(outPath.parent_path(), ec);
    if (ec) {
        if (outError) *outError = "create_directories failed: " + ec.message();
        return false;
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        if (outError) *outError = "failed to open file for write";
        return false;
    }

    try {
        out << j.dump(2) << "\n";
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

} // namespace game::video

