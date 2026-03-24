#include "game/runtime/startup/RuntimeStartupSession.h"

#include "engine/core/Environment.h"
#include "game/runtime/video/GpuAdapters.h"
#include "game/runtime/renderer/RendererBackendBootstrap.h"
#include "game/runtime/renderer/RendererStartupDiagnostics.h"
#include "game/runtime/startup/RuntimeStartupConfig.h"

#include <ostream>

namespace game::runtime::startup_session {

PreparedSession prepare(const std::string& prefsPath,
                        const std::optional<std::string>& envBackend,
                        std::ostream& logOut,
                        std::ostream& errOut) {
    game::video::Preferences mutablePrefs = game::video::loadPreferences(prefsPath);

    PreparedSession out;
    out.bootMenuScreen = game::runtime::startup_config::consumeBootMenuScreen(mutablePrefs);
    if (!out.bootMenuScreen.empty()) {
        std::string consumeErr;
        if (!game::video::savePreferences(mutablePrefs, prefsPath, &consumeErr)) {
            errOut << "[Video] Failed to clear one-shot boot menu screen: " << consumeErr << "\n";
        }
    }

    const auto resolvedRendererPref = game::runtime::startup_config::resolveRendererPreference(
        mutablePrefs,
        envBackend);
    if (resolvedRendererPref.overriddenByEnv) {
        logOut << "[Renderer] PAC_RENDER_BACKEND override: " << resolvedRendererPref.backendToken << "\n";
        logOut << "[Renderer] Note: env override is active; saved Display settings "
                  "(Render API) are ignored until PAC_RENDER_BACKEND is unset.\n";
    }

    out.requestedBackend = resolvedRendererPref.requestedBackend;
    out.requestedBackendName = resolvedRendererPref.requestedBackendName;
    out.vsyncEnabled = mutablePrefs.vsync;
    out.fpsCap = mutablePrefs.fpsCap;
    out.graphicsQuality = game::video::sanitizeGraphicsQuality(mutablePrefs.graphicsQuality);
    out.requireDiscreteGpu = mutablePrefs.requireDiscreteGpu;
    out.preferredGpuAdapter = mutablePrefs.preferredGpuAdapter;
    out.characterInkingEnabled = mutablePrefs.characterInking;
    out.audioMasterVolume = mutablePrefs.audioMasterVolume;
    out.audioMusicVolume = mutablePrefs.audioMusicVolume;
    out.audioSfxVolume = mutablePrefs.audioSfxVolume;
    out.audioVoiceVolume = mutablePrefs.audioVoiceVolume;
    out.audioMute = mutablePrefs.audioMute;

    const auto adapters = game::video::enumerateSystemGpuAdapters();
    out.availableGpuAdapters = game::runtime::startup_diag::collectGpuAdapterNames(adapters);
    game::runtime::startup_diag::logGpuAdapterInventory(
        adapters,
        out.preferredGpuAdapter,
        logOut);

    const auto backendSelection = game::runtime::backend_bootstrap::selectStartupBackend(
        out.requestedBackend,
        out.requestedBackendName);
    out.activeBackend = backendSelection.activeBackend;
    out.rendererBackendFallback = backendSelection.fallback;
    out.rendererBackendFallbackReason = backendSelection.fallbackReason;
    if (out.rendererBackendFallback) {
        logOut << "[Renderer] " << out.rendererBackendFallbackReason << "\n";
    }

    return out;
}

PreparedSession prepareFromEnvironment(const std::string& prefsPath,
                                       std::ostream& logOut,
                                       std::ostream& errOut) {
    return prepare(prefsPath, engine::env::get("PAC_RENDER_BACKEND"), logOut, errOut);
}

void applyToServices(const PreparedSession& session, EngineServices& services) {
    services.bootMenuScreen = session.bootMenuScreen;
    services.requestedRendererBackend = session.requestedBackendName;
    services.activeRendererBackend = game::video::rendererBackendName(session.activeBackend);
    services.rendererBackendFallback = session.rendererBackendFallback;
    services.rendererBackendFallbackReason = session.rendererBackendFallbackReason;
    services.vsyncEnabled = session.vsyncEnabled;
    services.fpsCap = session.fpsCap;
    services.graphicsQuality = game::video::sanitizeGraphicsQuality(session.graphicsQuality);
    services.graphicsQualityGeneration = 1u;
    services.requireDiscreteGpu = session.requireDiscreteGpu;
    services.preferredGpuAdapter = session.preferredGpuAdapter;
    services.characterInkingEnabled = session.characterInkingEnabled;
    services.audioMasterVolume = session.audioMasterVolume;
    services.audioMusicVolume = session.audioMusicVolume;
    services.audioSfxVolume = session.audioSfxVolume;
    services.audioVoiceVolume = session.audioVoiceVolume;
    services.audioMute = session.audioMute;
    services.availableGpuAdapters = session.availableGpuAdapters;
}

} // namespace game::runtime::startup_session

