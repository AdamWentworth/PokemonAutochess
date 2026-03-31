#include "game/runtime/loop/RuntimeGameRunnerLoopPolicy.h"

#include "game/runtime/loop/RuntimeLoopConfig.h"

#include <algorithm>
#include <ostream>

namespace game::runtime::runner_loop_policy {

Config readConfig(std::ostream& out, std::ostream& err) {
    Config config;
    config.maxFixedTicksPerFrame =
        game::runtime::loop_config::readMaxFixedTicksPerFrameFromEnvironment(err);
    out << "[Run] Fixed tick budget: " << config.maxFixedTicksPerFrame << " ticks/frame\n";

    config.autoQuit = game::runtime::auto_quit::fromEnvironment();
    if (config.autoQuit.enabled()) {
        out << "[Run] Auto-quit policy enabled:";
        if (config.autoQuit.maxSeconds > 0.0) {
            out << " seconds=" << config.autoQuit.maxSeconds;
        }
        if (config.autoQuit.maxFrames > 0) {
            out << " frames=" << config.autoQuit.maxFrames;
        }
        out << "\n";
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
    out << "[Run] Exiting main loop: "
        << game::runtime::loop_control::effectiveStopReason(loopState) << "\n";
}

} // namespace game::runtime::runner_loop_policy
