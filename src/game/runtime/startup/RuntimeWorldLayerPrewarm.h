#pragma once

#include "engine/utils/LogSink.h"

#include <functional>
#include <string>

namespace game::runtime::world_layer_prewarm {

struct Callbacks {
    std::function<void(const std::string&)> setTitle;
    std::function<void(float)> renderBootLoading;
    std::function<bool()> pumpPreloadEvents;
    std::function<void()> requestQuit;
    std::function<void(int, int)> renderWorldLayer;
};

void schedule(int& framesRemaining,
              int totalFrames,
              const Callbacks& callbacks,
              const engine::log::Sink& log);

void drainStartupFrames(int& framesRemaining,
                        int totalFrames,
                        int drawableW,
                        int drawableH,
                        const Callbacks& callbacks,
                        const engine::log::Sink& log);

void maybeRunDeferredFrame(int& framesRemaining,
                           int totalFrames,
                           bool flowRendersWorldLayer,
                           int drawableW,
                           int drawableH,
                           const Callbacks& callbacks,
                           const engine::log::Sink& log);

void restoreTitleAfterInit(int framesRemaining, const Callbacks& callbacks);

} // namespace game::runtime::world_layer_prewarm
