#include <filesystem>
#include <sstream>
#include <string>

#include "engine/core/EngineServices.h"
#include "game/runtime/startup/RuntimeStartupSession.h"
#include "game/runtime/video/VideoPreferences.h"

bool test_runtime_startup_session_contract(std::string& outFail) {
    namespace fs = std::filesystem;

    const fs::path prefsPath =
        fs::temp_directory_path() / "pac_runtime_startup_session_contract_video_settings.json";
    std::error_code removeError;
    fs::remove(prefsPath, removeError);

    {
        game::video::Preferences prefs;
        prefs.rendererBackend = "opengl";
        prefs.vsync = true;
        prefs.fpsCap = 144;
        prefs.graphicsQuality = static_cast<int>(game::video::GraphicsQuality::High);
        prefs.requireDiscreteGpu = true;
        prefs.preferredGpuAdapter = "RTX";
        prefs.characterInking = true;
        prefs.audioMasterVolume = 88;
        prefs.audioMusicVolume = 77;
        prefs.audioSfxVolume = 66;
        prefs.audioVoiceVolume = 55;
        prefs.audioMute = true;
        prefs.bootMenuScreen = "video";
        std::string saveErr;
        if (!game::video::savePreferences(prefs, prefsPath.string(), &saveErr)) {
            outFail = "failed to seed startup-session preferences: " + saveErr;
            return false;
        }
    }

    {
        std::ostringstream logs;
        std::ostringstream errs;
        const auto session = game::runtime::startup_session::prepare(
            prefsPath.string(),
            std::optional<std::string>("d3d12"),
            logs,
            errs);
        if (session.bootMenuScreen != "video" ||
            session.requestedBackend != game::video::RendererBackend::D3D12 ||
            session.activeBackend != game::video::RendererBackend::D3D12 ||
            !session.vsyncEnabled ||
            session.fpsCap != 144 ||
            session.graphicsQuality != static_cast<int>(game::video::GraphicsQuality::High) ||
            !session.requireDiscreteGpu ||
            session.preferredGpuAdapter != "RTX" ||
            !session.characterInkingEnabled ||
            session.audioMasterVolume != 88 ||
            session.audioMusicVolume != 77 ||
            session.audioSfxVolume != 66 ||
            session.audioVoiceVolume != 55 ||
            !session.audioMute ||
            logs.str().find("PAC_RENDER_BACKEND override") == std::string::npos ||
            !errs.str().empty()) {
            outFail = "prepare should consume one-shot prefs, preserve display settings, and honor an explicit backend override.";
            fs::remove(prefsPath, removeError);
            return false;
        }

        EngineServices services;
        game::runtime::startup_session::applyToServices(session, services);
        if (services.bootMenuScreen != "video" ||
            services.requestedRendererBackend != "d3d12" ||
            services.activeRendererBackend != "d3d12" ||
            !services.vsyncEnabled ||
            services.fpsCap != 144 ||
            services.graphicsQuality != static_cast<int>(game::video::GraphicsQuality::High) ||
            !services.requireDiscreteGpu ||
            services.preferredGpuAdapter != "RTX" ||
            !services.characterInkingEnabled ||
            services.audioMasterVolume != 88 ||
            services.audioMusicVolume != 77 ||
            services.audioSfxVolume != 66 ||
            services.audioVoiceVolume != 55 ||
            !services.audioMute ||
            services.availableGpuAdapters != session.availableGpuAdapters) {
            outFail = "applyToServices should copy prepared startup session state into EngineServices.";
            fs::remove(prefsPath, removeError);
            return false;
        }

        const auto saved = game::video::loadPreferences(prefsPath.string());
        if (!saved.bootMenuScreen.empty()) {
            outFail = "prepare should persist one-shot boot menu consumption back to the preference file.";
            fs::remove(prefsPath, removeError);
            return false;
        }
    }

    {
        game::video::Preferences prefs;
        prefs.rendererBackend = "vulkan";
        std::string saveErr;
        if (!game::video::savePreferences(prefs, prefsPath.string(), &saveErr)) {
            outFail = "failed to seed vulkan startup-session preferences: " + saveErr;
            fs::remove(prefsPath, removeError);
            return false;
        }

        std::ostringstream logs;
        std::ostringstream errs;
        const auto session = game::runtime::startup_session::prepare(
            prefsPath.string(),
            std::nullopt,
            logs,
            errs);
        if (session.activeBackend != game::video::RendererBackend::OpenGL ||
            !session.rendererBackendFallback ||
            session.rendererBackendFallbackReason.find("vulkan") == std::string::npos ||
            logs.str().find("falling back to OpenGL") == std::string::npos ||
            !errs.str().empty()) {
            outFail = "prepare should surface startup fallback state when the requested backend is not implemented.";
            fs::remove(prefsPath, removeError);
            return false;
        }
    }

    fs::remove(prefsPath, removeError);
    return true;
}

