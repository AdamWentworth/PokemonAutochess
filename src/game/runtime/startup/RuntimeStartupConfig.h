#pragma once

#include <iosfwd>
#include <optional>
#include <string>

#include "game/runtime/video/VideoPreferences.h"

namespace game::runtime::startup_config {

struct StartupVideoOverride {
    bool hasWidth = false;
    bool hasHeight = false;
    bool hasFullscreen = false;
    int width = 0;
    int height = 0;
    bool fullscreen = false;

    bool enabled() const {
        return hasWidth || hasHeight || hasFullscreen;
    }
};

struct StartupVideoMode {
    int width = 0;
    int height = 0;
    bool fullscreen = false;
};

struct StartupPresentationOverride {
    bool hasVsync = false;
    bool hasFpsCap = false;
    bool vsyncEnabled = false;
    int fpsCap = 0;

    bool enabled() const {
        return hasVsync || hasFpsCap;
    }
};

struct ResolvedRendererPreference {
    std::string backendToken;
    game::video::RendererBackend requestedBackend = game::video::RendererBackend::Auto;
    std::string requestedBackendName = "auto";
    bool overriddenByEnv = false;
};

std::string consumeBootMenuScreen(game::video::Preferences& prefs);

ResolvedRendererPreference resolveRendererPreference(
    const game::video::Preferences& prefs,
    const std::optional<std::string>& envBackend);

StartupVideoOverride readStartupVideoOverride(std::ostream& err);
StartupPresentationOverride readStartupPresentationOverride(std::ostream& err);

StartupVideoMode resolveStartupVideoMode(const StartupVideoOverride& overrideValues,
                                         int currentWidth,
                                         int currentHeight,
                                         bool currentFullscreen);

} // namespace game::runtime::startup_config

