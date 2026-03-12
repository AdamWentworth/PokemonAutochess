#include <cmath>
#include <string>

#include "engine/core/EngineServices.h"
#include "game/runtime/RuntimeFixedStepPhase.h"

bool test_runtime_fixed_step_phase_contract(std::string& outFail) {
    EngineServices services;

    {
        int fixedUpdateCalls = 0;
        const auto result = game::runtime::fixed_step_phase::execute(
            (1.0 / 60.0) * 3.5,
            1.0 / 60.0,
            2,
            services,
            [&](float) {
                ++fixedUpdateCalls;
                services.frameFixedBreakdown.combatMs += 1.0f;
            });
        if (fixedUpdateCalls != 2 ||
            result.fixedTicks != 2 ||
            result.fixedTicksDropped != 1 ||
            result.accumulator <= 0.0 ||
            result.accumulator >= (1.0 / 60.0) ||
            std::fabs(result.fixedBreakdown.combatMs - 2.0f) > 0.0001f) {
            outFail = "execute should run up to the fixed tick budget, drop excess whole ticks, and preserve the fixed breakdown.";
            return false;
        }
    }

    {
        services.frameFixedBreakdown.combatMs = 9.0f;
        int fixedUpdateCalls = 0;
        const auto result = game::runtime::fixed_step_phase::execute(
            0.005,
            1.0 / 60.0,
            4,
            services,
            [&](float) { ++fixedUpdateCalls; });
        if (fixedUpdateCalls != 0 ||
            result.fixedTicks != 0 ||
            result.fixedTicksDropped != 0 ||
            std::fabs(result.accumulator - 0.005) > 0.000001 ||
            std::fabs(result.fixedBreakdown.combatMs) > 0.0001f) {
            outFail = "execute should reset stale fixed breakdown data even when no fixed tick runs.";
            return false;
        }
    }

    return true;
}
