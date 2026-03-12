#include "game/runtime/RuntimeStartupVideoOverride.h"

namespace game::runtime::startup_video_override {

ApplyResult apply(const startup_config::StartupVideoOverride& overrideValues,
                  const std::function<GameContext::VideoMode()>& queryVideoMode,
                  const std::function<bool(int, int, bool)>& applyVideoMode) {
    ApplyResult out;
    if (!overrideValues.enabled() || !queryVideoMode || !applyVideoMode) {
        return out;
    }

    const auto currentMode = queryVideoMode();
    const auto targetMode = game::runtime::startup_config::resolveStartupVideoMode(
        overrideValues,
        currentMode.width,
        currentMode.height,
        currentMode.fullscreen);

    out.attempted = true;
    if (!applyVideoMode(targetMode.width, targetMode.height, targetMode.fullscreen)) {
        out.message = "[Video] Failed to apply startup override video mode.";
        return out;
    }

    const auto appliedMode = queryVideoMode();
    out.applied = true;
    out.message = std::string("[Video] Startup override applied: ") +
        (appliedMode.fullscreen ? "Fullscreen" : "Windowed") + " " +
        std::to_string(appliedMode.width) + "x" + std::to_string(appliedMode.height);
    return out;
}

} // namespace game::runtime::startup_video_override
