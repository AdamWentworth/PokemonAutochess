#include "game/runtime/startup/RuntimeStartupConfig.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <ostream>

#include "engine/core/Environment.h"

namespace {

std::string toLowerCopy(std::string s) {
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool parseEnvIntValue(const char* envName, int& outValue, std::ostream& err) {
    const auto raw = engine::env::get(envName);
    if (!raw.has_value()) return false;
    try {
        const long long parsed = std::stoll(*raw);
        if (parsed < static_cast<long long>(std::numeric_limits<int>::min()) ||
            parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
            err << "[Video] Ignoring out-of-range " << envName << " value: " << *raw << "\n";
            return false;
        }
        outValue = static_cast<int>(parsed);
        return true;
    } catch (...) {
        err << "[Video] Ignoring invalid " << envName << " value: " << *raw << "\n";
        return false;
    }
}

bool parseEnvBoolValue(const char* envName, bool& outValue, std::ostream& err) {
    const auto raw = engine::env::get(envName);
    if (!raw.has_value()) return false;
    const std::string token = toLowerCopy(*raw);
    if (token == "1" || token == "true" || token == "on" || token == "yes") {
        outValue = true;
        return true;
    }
    if (token == "0" || token == "false" || token == "off" || token == "no") {
        outValue = false;
        return true;
    }
    err << "[Video] Ignoring invalid " << envName << " value: " << *raw << "\n";
    return false;
}

} // namespace

namespace game::runtime::startup_config {

std::string consumeBootMenuScreen(game::video::Preferences& prefs) {
    const std::string out = prefs.bootMenuScreen;
    if (!out.empty()) {
        prefs.bootMenuScreen.clear();
    }
    return out;
}

ResolvedRendererPreference resolveRendererPreference(
    const game::video::Preferences& prefs,
    const std::optional<std::string>& envBackend) {
    ResolvedRendererPreference out;
    out.backendToken = prefs.rendererBackend;
    if (envBackend.has_value()) {
        out.backendToken = *envBackend;
        out.overriddenByEnv = true;
    }
    out.requestedBackend = game::video::parseRendererBackend(out.backendToken);
    out.requestedBackendName = game::video::rendererBackendName(out.requestedBackend);
    return out;
}

StartupVideoOverride readStartupVideoOverride(std::ostream& err) {
    StartupVideoOverride out;
    out.hasWidth = parseEnvIntValue("PAC_VIDEO_WIDTH", out.width, err);
    out.hasHeight = parseEnvIntValue("PAC_VIDEO_HEIGHT", out.height, err);
    out.hasFullscreen = parseEnvBoolValue("PAC_VIDEO_FULLSCREEN", out.fullscreen, err);
    return out;
}

StartupVideoMode resolveStartupVideoMode(const StartupVideoOverride& overrideValues,
                                         int currentWidth,
                                         int currentHeight,
                                         bool currentFullscreen) {
    StartupVideoMode out;
    out.width = overrideValues.hasWidth ? overrideValues.width : currentWidth;
    out.height = overrideValues.hasHeight ? overrideValues.height : currentHeight;
    out.fullscreen = overrideValues.hasFullscreen ? overrideValues.fullscreen : currentFullscreen;
    return out;
}

} // namespace game::runtime::startup_config
