#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "game/runtime/startup/RuntimeStartupConfig.h"
#include "TestEnvVarUtils.h"

namespace {
using test::env_utils::ScopedEnvVar;
using test::env_utils::setEnvVar;

} // namespace

bool test_runtime_startup_config_contract(std::string& outFail) {
    using game::runtime::startup_config::consumeBootMenuScreen;
    using game::runtime::startup_config::readStartupPresentationOverride;
    using game::runtime::startup_config::resolveRendererPreference;
    using game::runtime::startup_config::readStartupVideoOverride;
    using game::runtime::startup_config::resolveStartupVideoMode;
    using game::video::Preferences;
    using game::video::RendererBackend;

    {
        Preferences prefs;
        prefs.bootMenuScreen = "video";
        const std::string consumed = consumeBootMenuScreen(prefs);
        if (consumed != "video" || !prefs.bootMenuScreen.empty()) {
            outFail = "consumeBootMenuScreen should return and clear the one-shot screen.";
            return false;
        }
    }

    {
        Preferences prefs;
        prefs.rendererBackend = "opengl";
        const auto resolved = resolveRendererPreference(prefs, std::nullopt);
        if (resolved.backendToken != "opengl" ||
            resolved.requestedBackend != RendererBackend::OpenGL ||
            resolved.requestedBackendName != "opengl" ||
            resolved.overriddenByEnv) {
            outFail = "resolveRendererPreference should preserve stored backend without env override.";
            return false;
        }
    }

    {
        Preferences prefs;
        prefs.rendererBackend = "opengl";
        const auto resolved = resolveRendererPreference(prefs, std::optional<std::string>("d3d12"));
        if (resolved.backendToken != "d3d12" ||
            resolved.requestedBackend != RendererBackend::D3D12 ||
            resolved.requestedBackendName != "d3d12" ||
            !resolved.overriddenByEnv) {
            outFail = "resolveRendererPreference should prefer PAC_RENDER_BACKEND override.";
            return false;
        }
    }

    ScopedEnvVar widthGuard("PAC_VIDEO_WIDTH");
    ScopedEnvVar heightGuard("PAC_VIDEO_HEIGHT");
    ScopedEnvVar fullscreenGuard("PAC_VIDEO_FULLSCREEN");
    ScopedEnvVar vsyncGuard("PAC_VIDEO_VSYNC");
    ScopedEnvVar fpsCapGuard("PAC_VIDEO_FPS_CAP");

    {
        setEnvVar("PAC_VIDEO_WIDTH", "1600");
        setEnvVar("PAC_VIDEO_HEIGHT", "900");
        setEnvVar("PAC_VIDEO_FULLSCREEN", "true");

        std::ostringstream errs;
        const auto overrideValues = readStartupVideoOverride(errs);
        if (!overrideValues.hasWidth || overrideValues.width != 1600 ||
            !overrideValues.hasHeight || overrideValues.height != 900 ||
            !overrideValues.hasFullscreen || !overrideValues.fullscreen ||
            !overrideValues.enabled() ||
            !errs.str().empty()) {
            outFail = "readStartupVideoOverride should parse valid width/height/fullscreen overrides.";
            return false;
        }

        const auto mode = resolveStartupVideoMode(overrideValues, 1280, 720, false);
        if (mode.width != 1600 || mode.height != 900 || !mode.fullscreen) {
            outFail = "resolveStartupVideoMode should apply explicit override values.";
            return false;
        }

        setEnvVar("PAC_VIDEO_VSYNC", "false");
        setEnvVar("PAC_VIDEO_FPS_CAP", "0");
        const auto presentationOverride = readStartupPresentationOverride(errs);
        if (!presentationOverride.hasVsync ||
            presentationOverride.vsyncEnabled ||
            !presentationOverride.hasFpsCap ||
            presentationOverride.fpsCap != 0 ||
            !presentationOverride.enabled()) {
            outFail = "readStartupPresentationOverride should parse valid VSync/FPS overrides.";
            return false;
        }
    }

    {
        setEnvVar("PAC_VIDEO_WIDTH", "abc");
        setEnvVar("PAC_VIDEO_HEIGHT", nullptr);
        setEnvVar("PAC_VIDEO_FULLSCREEN", "maybe");
        setEnvVar("PAC_VIDEO_VSYNC", "maybe");
        setEnvVar("PAC_VIDEO_FPS_CAP", "bogus");

        std::ostringstream errs;
        const auto overrideValues = readStartupVideoOverride(errs);
        const auto presentationOverride = readStartupPresentationOverride(errs);
        if (overrideValues.hasWidth || overrideValues.hasHeight || overrideValues.hasFullscreen || overrideValues.enabled()) {
            outFail = "readStartupVideoOverride should ignore invalid or missing env overrides.";
            return false;
        }
        if (presentationOverride.hasVsync || presentationOverride.hasFpsCap || presentationOverride.enabled()) {
            outFail = "readStartupPresentationOverride should ignore invalid env overrides.";
            return false;
        }
        const std::string errText = errs.str();
        if (errText.find("PAC_VIDEO_WIDTH") == std::string::npos ||
            errText.find("PAC_VIDEO_FULLSCREEN") == std::string::npos ||
            errText.find("PAC_VIDEO_VSYNC") == std::string::npos ||
            errText.find("PAC_VIDEO_FPS_CAP") == std::string::npos) {
            outFail = "startup env override readers should report invalid env names in diagnostics.";
            return false;
        }

        const auto mode = resolveStartupVideoMode(overrideValues, 1280, 720, false);
        if (mode.width != 1280 || mode.height != 720 || mode.fullscreen) {
            outFail = "resolveStartupVideoMode should preserve current mode when no overrides are present.";
            return false;
        }
    }

    return true;
}
