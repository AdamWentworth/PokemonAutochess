#include "game/runtime/RuntimeFixedStepPhase.h"

#include "game/runtime/RuntimeLoopConfig.h"

#include <chrono>

namespace game::runtime::fixed_step_phase {

Result execute(double accumulator,
               double timeStep,
               int maxFixedTicksPerFrame,
               EngineServices& services,
               const std::function<void(float)>& fixedUpdate) {
    using clock = std::chrono::high_resolution_clock;

    Result out;
    out.accumulator = accumulator;
    services.frameFixedBreakdown = {};

    const auto fixedStart = clock::now();
    while (out.accumulator >= timeStep && out.fixedTicks < maxFixedTicksPerFrame) {
        const auto fixedTickStart = clock::now();
        fixedUpdate(static_cast<float>(timeStep));
        out.fixedTickWorkMs +=
            std::chrono::duration<double, std::milli>(clock::now() - fixedTickStart).count();
        out.accumulator -= timeStep;
        ++out.fixedTicks;
    }
    if (out.accumulator >= timeStep) {
        out.fixedTicksDropped =
            game::runtime::loop_config::dropExcessFixedTicks(out.accumulator, timeStep);
    }

    out.fixedMs = std::chrono::duration<double, std::milli>(clock::now() - fixedStart).count();
    out.fixedBreakdown = services.frameFixedBreakdown;
    return out;
}

} // namespace game::runtime::fixed_step_phase
