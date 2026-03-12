#include <filesystem>
#include <sstream>
#include <string>

#include "engine/core/EngineServices.h"
#include "game/runtime/startup/RuntimeStartupSession.h"
#include "game/runtime/VideoPreferences.h"

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
        prefs.requireDiscreteGpu = true;
        prefs.preferredGpuAdapter = "RTX";
        prefs.characterInking = true;
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
            !session.requireDiscreteGpu ||
            session.preferredGpuAdapter != "RTX" ||
            !session.characterInkingEnabled ||
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
            !services.requireDiscreteGpu ||
            services.preferredGpuAdapter != "RTX" ||
            !services.characterInkingEnabled ||
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
