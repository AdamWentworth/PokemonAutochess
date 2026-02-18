#pragma once

#include "engine/core/Environment.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace game::runtime::auto_quit {

struct Policy {
    double maxSeconds = 0.0;
    int maxFrames = 0;

    bool enabled() const {
        return maxSeconds > 0.0 || maxFrames > 0;
    }
};

inline std::optional<double> parsePositiveDouble(std::string_view text) {
    if (text.empty()) return std::nullopt;
    try {
        const std::string value(text);
        const double parsed = std::stod(value);
        if (!(parsed > 0.0)) return std::nullopt;
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<int> parsePositiveInt(std::string_view text) {
    if (text.empty()) return std::nullopt;
    try {
        const std::string value(text);
        const int parsed = std::stoi(value);
        if (parsed <= 0) return std::nullopt;
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

inline Policy fromEnvironment() {
    Policy out;
    if (const auto seconds = engine::env::get("PAC_AUTO_QUIT_SECONDS")) {
        if (const auto parsed = parsePositiveDouble(*seconds)) {
            out.maxSeconds = std::clamp(*parsed, 0.01, 3600.0);
        }
    }
    if (const auto frames = engine::env::get("PAC_AUTO_QUIT_FRAMES")) {
        if (const auto parsed = parsePositiveInt(*frames)) {
            out.maxFrames = std::clamp(*parsed, 1, 500000);
        }
    }
    return out;
}

inline bool shouldTrigger(const Policy& policy, double elapsedSeconds, int renderedFrames) {
    if (policy.maxFrames > 0 && renderedFrames >= policy.maxFrames) return true;
    if (policy.maxSeconds > 0.0 && elapsedSeconds >= policy.maxSeconds) return true;
    return false;
}

} // namespace game::runtime::auto_quit
