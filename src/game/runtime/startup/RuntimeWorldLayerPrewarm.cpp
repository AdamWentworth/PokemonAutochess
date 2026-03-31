#include "game/runtime/startup/RuntimeWorldLayerPrewarm.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace game::runtime::world_layer_prewarm {

namespace {

bool pumpPreloadEventsOrQuit(const Callbacks& callbacks) {
    if (!callbacks.pumpPreloadEvents) {
        return true;
    }
    if (callbacks.pumpPreloadEvents()) {
        return true;
    }
    if (callbacks.requestQuit) {
        callbacks.requestQuit();
    }
    return false;
}

void maybeSetFrameTitle(const Callbacks& callbacks, int frameIndex, int totalFrames) {
    if (!callbacks.setTitle) {
        return;
    }
    callbacks.setTitle(
        std::string("PokemonAutochess - Loading world/board ") +
        std::to_string(frameIndex) + "/" + std::to_string(totalFrames));
}

void maybeRenderBootProgress(const Callbacks& callbacks, int frameIndex, int totalFrames) {
    if (!callbacks.renderBootLoading || totalFrames <= 0) {
        return;
    }
    const float progress =
        0.98f +
        (static_cast<float>(frameIndex) / static_cast<float>(totalFrames)) * 0.02f;
    callbacks.renderBootLoading(std::min(1.0f, progress));
}

void runFrame(int& framesRemaining,
              int totalFrames,
              int drawableW,
              int drawableH,
              const Callbacks& callbacks,
              const engine::log::Sink& log) {
    if (framesRemaining <= 0 || totalFrames <= 0 || !callbacks.renderWorldLayer) {
        return;
    }
    const int frameIndex = totalFrames - framesRemaining + 1;
    maybeSetFrameTitle(callbacks, frameIndex, totalFrames);
    log.info("[Init] World/board prewarm frame " +
             std::to_string(frameIndex) + "/" + std::to_string(totalFrames) + " begin");
    const auto t0 = std::chrono::high_resolution_clock::now();
    callbacks.renderWorldLayer(drawableW, drawableH);
    const auto t1 = std::chrono::high_resolution_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::ostringstream timing;
    timing << std::fixed << std::setprecision(1) << ms;
    log.info("[Init] World/board prewarm frame " +
             std::to_string(frameIndex) + "/" + std::to_string(totalFrames) +
             " complete: time=" + timing.str() + "ms");
    --framesRemaining;
}

} // namespace

void schedule(int& framesRemaining,
              int totalFrames,
              const Callbacks& callbacks,
              const engine::log::Sink& log) {
    if (totalFrames <= 0) {
        framesRemaining = 0;
        return;
    }
    framesRemaining = totalFrames;
    if (callbacks.setTitle) {
        callbacks.setTitle("PokemonAutochess - Loading world/board...");
    }
    if (callbacks.renderBootLoading) {
        callbacks.renderBootLoading(0.98f);
    }
    log.info("[Init] World/board prewarm scheduled: frames=" +
             std::to_string(framesRemaining) +
             " (board grid + world render path)");
}

void drainStartupFrames(int& framesRemaining,
                        int totalFrames,
                        int drawableW,
                        int drawableH,
                        const Callbacks& callbacks,
                        const engine::log::Sink& log) {
    if (framesRemaining <= 0 || drawableW <= 0 || drawableH <= 0 || !callbacks.renderWorldLayer) {
        return;
    }
    while (framesRemaining > 0) {
        const int frameIndex = totalFrames - framesRemaining + 1;
        maybeRenderBootProgress(callbacks, frameIndex, totalFrames);
        runFrame(framesRemaining, totalFrames, drawableW, drawableH, callbacks, log);
        if (!pumpPreloadEventsOrQuit(callbacks)) {
            break;
        }
    }
}

void maybeRunDeferredFrame(int& framesRemaining,
                           int totalFrames,
                           bool flowRendersWorldLayer,
                           int drawableW,
                           int drawableH,
                           const Callbacks& callbacks,
                           const engine::log::Sink& log) {
    if (framesRemaining <= 0 || flowRendersWorldLayer || drawableW <= 0 || drawableH <= 0) {
        return;
    }
    runFrame(framesRemaining, totalFrames, drawableW, drawableH, callbacks, log);
    if (framesRemaining <= 0 && callbacks.setTitle) {
        callbacks.setTitle("Pokemon Autochess");
    }
}

void restoreTitleAfterInit(int framesRemaining, const Callbacks& callbacks) {
    if (!callbacks.setTitle) {
        return;
    }
    if (framesRemaining > 0) {
        callbacks.setTitle("PokemonAutochess - Loading world/board...");
    } else {
        callbacks.setTitle("Pokemon Autochess");
    }
}

} // namespace game::runtime::world_layer_prewarm
