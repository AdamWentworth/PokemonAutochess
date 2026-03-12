#pragma once

#include "engine/core/EngineServices.h"

#include <functional>

namespace game::runtime::fixed_step_phase {

struct Result {
    double accumulator = 0.0;
    double fixedMs = 0.0;
    double fixedTickWorkMs = 0.0;
    int fixedTicks = 0;
    int fixedTicksDropped = 0;
    EngineFixedPerfBreakdown fixedBreakdown{};
};

Result execute(double accumulator,
               double timeStep,
               int maxFixedTicksPerFrame,
               EngineServices& services,
               const std::function<void(float)>& fixedUpdate);

} // namespace game::runtime::fixed_step_phase
