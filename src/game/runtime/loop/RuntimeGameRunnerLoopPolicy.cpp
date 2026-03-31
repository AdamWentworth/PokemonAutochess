#include "game/runtime/loop/RuntimeGameRunnerLoopPolicy.h"

#include "engine/utils/LogSink.h"
#include "game/runtime/loop/RuntimeLoopConfig.h"

#include <algorithm>
#include <ostream>
#include <sstream>

namespace game::runtime::runner_loop_policy {

Config readConfig(std::ostream& out, std::ostream& err) {
    engine::log::Sink log("Run", &out, &err);
    Config config;
    config.maxFixedTicksPerFrame =
        game::runtime::loop_config::readMaxFixedTicksPerFrameFromEnvironment(err);
    log.info("[Run] Fixed tick budget: " + std::to_string(config.maxFixedTicksPerFrame) +
             " ticks/frame");

    config.autoQuit = game::runtime::auto_quit::fromEnvironment();
    if (config.autoQuit.enabled()) {
        std::ostringstream line;
        line << "[Run] Auto-quit policy enabled:";
        if (config.autoQuit.maxSeconds > 0.0) {
            line << " seconds=" << config.autoQuit.maxSeconds;
        }
        if (config.autoQuit.maxFrames > 0) {
            line << " frames=" << config.autoQuit.maxFrames;
        }
        log.info(line.str());
    }

    return config;
}

State makeInitialState(const Config& config, const TimePoint& previous) {
    State state;
    state.config = config;
    state.previous = previous;
    return state;
}

FrameStart beginFrame(State& state) {
    const auto now = Clock::now();
    const double frameDt =
        game::runtime::loop_config::clampFrameDeltaSeconds(
            std::chrono::duration<double>(now - state.previous).count());
    state.previous = now;
    state.accumulator += frameDt;
    return FrameStart{
        .frameDt = frameDt,
        .frameStart = now,
    };
}

int maxFixedTicksPerFrame(const State& state) {
    return state.config.maxFixedTicksPerFrame;
}

double accumulator(const State& state) {
    return state.accumulator;
}

void setAccumulator(State& state, double accumulatorValue) {
    state.accumulator = std::max(0.0, accumulatorValue);
}

void finishFrame(State& state,
                 game::runtime::loop_control::State& loopState,
                 double frameDt,
                 const TimePoint& frameStart,
                 const std::function<void(const TimePoint&)>& enforceFrameCap) {
    game::runtime::loop_control::notePresentedFrame(loopState, frameDt);
    if (state.config.autoQuit.enabled()) {
        game::runtime::loop_control::applyAutoQuit(state.config.autoQuit, loopState);
    }
    if (game::runtime::loop_control::isRunning(loopState) && enforceFrameCap) {
        enforceFrameCap(frameStart);
    }
}

void logExit(const game::runtime::loop_control::State& loopState,
             std::ostream& out) {
    engine::log::Sink log("Run", &out, nullptr);
    log.info("[Run] Exiting main loop: " +
             game::runtime::loop_control::effectiveStopReason(loopState));
}

} // namespace game::runtime::runner_loop_policy
