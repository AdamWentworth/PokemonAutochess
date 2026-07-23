#include <filesystem>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "engine/core/EngineServices.h"
#include "game/runtime/startup/RuntimeStartupSession.h"
#include "game/runtime/video/VideoPreferences.h"
#include "TestEnvVarUtils.h"

namespace {
using test::env_utils::ScopedEnvVar;
using test::env_utils::setEnvVar;

} // namespace

bool test_runtime_startup_session_contract(std::string& outFail) {
    namespace fs = std::filesystem;

    const fs::path prefsPath =
        fs::temp_directory_path() / "pac_runtime_startup_session_contract_video_settings.json";
    ScopedEnvVar vsyncGuard("PAC_VIDEO_VSYNC");
    ScopedEnvVar fpsCapGuard("PAC_VIDEO_FPS_CAP");
    ScopedEnvVar inkingGuard("PAC_VIDEO_CHARACTER_INKING");
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
        setEnvVar("PAC_VIDEO_VSYNC", "false");
        setEnvVar("PAC_VIDEO_FPS_CAP", "0");
        setEnvVar("PAC_VIDEO_CHARACTER_INKING", "false");
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
            session.vsyncEnabled ||
            session.fpsCap != 0 ||
            session.graphicsQuality != static_cast<int>(game::video::GraphicsQuality::High) ||
            !session.requireDiscreteGpu ||
            session.preferredGpuAdapter != "RTX" ||
            session.characterInkingEnabled ||
            session.audioMasterVolume != 88 ||
            session.audioMusicVolume != 77 ||
            session.audioSfxVolume != 66 ||
            session.audioVoiceVolume != 55 ||
            !session.audioMute ||
            logs.str().find("PAC_RENDER_BACKEND override") == std::string::npos ||
            logs.str().find("PAC_VIDEO_VSYNC override: Off") == std::string::npos ||
            logs.str().find("PAC_VIDEO_FPS_CAP override: 0") == std::string::npos ||
            logs.str().find("PAC_VIDEO_CHARACTER_INKING override: Off") == std::string::npos ||
            !errs.str().empty()) {
            outFail = "prepare should consume one-shot prefs, preserve display settings, and honor explicit backend/presentation overrides.";
            fs::remove(prefsPath, removeError);
            return false;
        }

        EngineServices services;
        game::runtime::startup_session::applyToServices(session, services);
        if (services.bootMenuScreen != "video" ||
            services.requestedRendererBackend != "d3d12" ||
            services.activeRendererBackend != "d3d12" ||
            services.vsyncEnabled ||
            services.fpsCap != 0 ||
            services.graphicsQuality != static_cast<int>(game::video::GraphicsQuality::High) ||
            !services.requireDiscreteGpu ||
            services.preferredGpuAdapter != "RTX" ||
            services.characterInkingEnabled ||
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
        if (session.requestedBackend != game::video::RendererBackend::Vulkan ||
            session.activeBackend != game::video::RendererBackend::Vulkan ||
            session.rendererBackendFallback ||
            !session.rendererBackendFallbackReason.empty() ||
            logs.str().find("falling back to OpenGL") != std::string::npos ||
            !errs.str().empty()) {
            outFail = "prepare should preserve Vulkan as an implemented startup backend.";
            fs::remove(prefsPath, removeError);
            return false;
        }
    }

    fs::remove(prefsPath, removeError);
    return true;
}

