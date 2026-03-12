#include <cmath>
#include <optional>
#include <sstream>
#include <string>

#include "game/runtime/RuntimeLoopConfig.h"

bool test_runtime_loop_config_contract(std::string& outFail) {
    using game::runtime::loop_config::clampFrameDeltaSeconds;
    using game::runtime::loop_config::dropExcessFixedTicks;
    using game::runtime::loop_config::resolveMaxFixedTicksPerFrame;

    {
        std::ostringstream errs;
        if (resolveMaxFixedTicksPerFrame(std::nullopt, errs) != 4 || !errs.str().empty()) {
            outFail = "resolveMaxFixedTicksPerFrame should default to 4 without an env override.";
            return false;
        }
    }

    {
        std::ostringstream errs;
        if (resolveMaxFixedTicksPerFrame(std::optional<std::string>("8"), errs) != 8 ||
            !errs.str().empty()) {
            outFail = "resolveMaxFixedTicksPerFrame should preserve valid explicit values.";
            return false;
        }
    }

    {
        std::ostringstream errs;
        if (resolveMaxFixedTicksPerFrame(std::optional<std::string>("0"), errs) != 1 ||
            resolveMaxFixedTicksPerFrame(std::optional<std::string>("999"), errs) != 120 ||
            !errs.str().empty()) {
            outFail = "resolveMaxFixedTicksPerFrame should clamp parsed values into the supported range.";
            return false;
        }
    }

    {
        std::ostringstream errs;
        if (resolveMaxFixedTicksPerFrame(std::optional<std::string>("abc"), errs) != 4 ||
            errs.str().find("PAC_MAX_FIXED_TICKS_PER_FRAME") == std::string::npos) {
            outFail = "resolveMaxFixedTicksPerFrame should ignore invalid values with diagnostics.";
            return false;
        }
    }

    {
        std::ostringstream errs;
        if (resolveMaxFixedTicksPerFrame(std::optional<std::string>("99999999999999999999"), errs) != 4 ||
            errs.str().find("PAC_MAX_FIXED_TICKS_PER_FRAME") == std::string::npos) {
            outFail = "resolveMaxFixedTicksPerFrame should ignore out-of-range values with diagnostics.";
            return false;
        }
    }

    if (std::fabs(clampFrameDeltaSeconds(0.10) - 0.10) > 0.000001 ||
        std::fabs(clampFrameDeltaSeconds(1.50) - 0.25) > 0.000001) {
        outFail = "clampFrameDeltaSeconds should cap runaway frame deltas at 0.25 seconds.";
        return false;
    }

    {
        double accumulator = (1.0 / 60.0) * 3.5;
        const int dropped = dropExcessFixedTicks(accumulator, 1.0 / 60.0);
        if (dropped != 3 || accumulator <= 0.0 || accumulator >= (1.0 / 60.0)) {
            outFail = "dropExcessFixedTicks should preserve only the fractional fixed-step remainder.";
            return false;
        }
    }

    {
        double accumulator = 0.005;
        const int dropped = dropExcessFixedTicks(accumulator, 1.0 / 60.0);
        if (dropped != 0 || std::fabs(accumulator - 0.005) > 0.000001) {
            outFail = "dropExcessFixedTicks should leave small accumulators unchanged.";
            return false;
        }
    }

    return true;
}
