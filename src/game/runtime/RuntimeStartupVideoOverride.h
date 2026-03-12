#pragma once

#include "engine/core/GameContext.h"
#include "game/runtime/RuntimeStartupConfig.h"

#include <functional>
#include <string>

namespace game::runtime::startup_video_override {

struct ApplyResult {
    bool attempted = false;
    bool applied = false;
    std::string message;
};

ApplyResult apply(const startup_config::StartupVideoOverride& overrideValues,
                  const std::function<GameContext::VideoMode()>& queryVideoMode,
                  const std::function<bool(int, int, bool)>& applyVideoMode);

} // namespace game::runtime::startup_video_override
