#pragma once

#include "game/runtime/GameRuntimeServices.h"

#include <functional>

namespace game::runtime::fixed_step_phase {

struct Result {
    double accumulator = 0.0;
    double fixedMs = 0.0;
    double fixedTickWorkMs = 0.0;
    int fixedTicks = 0;
    int fixedTicksDropped = 0;
    GameFixedPerfBreakdown fixedBreakdown{};
};

Result execute(double accumulator,
               double timeStep,
               int maxFixedTicksPerFrame,
               GameRuntimeServices &services,
               const std::function<void(float)> &fixedUpdate);

} // namespace game::runtime::fixed_step_phase
