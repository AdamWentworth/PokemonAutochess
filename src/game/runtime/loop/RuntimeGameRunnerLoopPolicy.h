#pragma once

#include "game/runtime/AutoQuitPolicy.h"
#include "game/runtime/loop/RuntimeLoopControl.h"

#include <chrono>
#include <functional>
#include <iosfwd>

namespace game::runtime::runner_loop_policy {

using Clock = std::chrono::high_resolution_clock;
using TimePoint = Clock::time_point;

struct Config {
    int maxFixedTicksPerFrame = 4;
    game::runtime::auto_quit::Policy autoQuit{};
    double fixedFrameDeltaSeconds = 0.0;
};

struct State {
    Config config{};
    TimePoint previous = Clock::now();
    double accumulator = 0.0;
};

struct FrameStart {
    double frameDt = 0.0;
    TimePoint frameStart{};
};

Config readConfig(std::ostream& out, std::ostream& err);

State makeInitialState(const Config& config,
                       const TimePoint& previous = Clock::now());

FrameStart beginFrame(State& state);

int maxFixedTicksPerFrame(const State& state);

double accumulator(const State& state);

void setAccumulator(State& state, double accumulator);

void finishFrame(State& state,
                 game::runtime::loop_control::State& loopState,
                 double frameDt,
                 const TimePoint& frameStart,
                 const std::function<void(const TimePoint&)>& enforceFrameCap);

void logExit(const game::runtime::loop_control::State& loopState,
             std::ostream& out);

} // namespace game::runtime::runner_loop_policy
