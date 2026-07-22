#include "game/runtime/loop/RuntimeLoopConfig.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ostream>

#include "engine/core/Environment.h"

namespace game::runtime::loop_config {

int resolveMaxFixedTicksPerFrame(const std::optional<std::string>& rawValue, std::ostream& err) {
    if (!rawValue.has_value()) {
        return 4;
    }

    try {
        const long long parsed = std::stoll(*rawValue);
        if (parsed < static_cast<long long>(std::numeric_limits<int>::min()) ||
            parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
            err << "[Video] Ignoring out-of-range PAC_MAX_FIXED_TICKS_PER_FRAME value: "
                << *rawValue << "\n";
            return 4;
        }
        return std::clamp(static_cast<int>(parsed), 1, 120);
    } catch (...) {
        err << "[Video] Ignoring invalid PAC_MAX_FIXED_TICKS_PER_FRAME value: "
            << *rawValue << "\n";
        return 4;
    }
}

int readMaxFixedTicksPerFrameFromEnvironment(std::ostream& err) {
    return resolveMaxFixedTicksPerFrame(engine::env::get("PAC_MAX_FIXED_TICKS_PER_FRAME"), err);
}

double resolveFixedFrameDeltaSeconds(const std::optional<std::string>& rawValue,
                                     std::ostream& err) {
    if (!rawValue.has_value()) {
        return 0.0;
    }

    try {
        std::size_t parsedCharacters = 0u;
        const double parsed = std::stod(*rawValue, &parsedCharacters);
        if (parsedCharacters != rawValue->size() ||
            !std::isfinite(parsed) ||
            parsed <= 0.0 ||
            parsed > 0.25) {
            err << "[Run] Ignoring out-of-range PAC_FIXED_FRAME_DT_SECONDS value: "
                << *rawValue << "\n";
            return 0.0;
        }
        return parsed;
    } catch (...) {
        err << "[Run] Ignoring invalid PAC_FIXED_FRAME_DT_SECONDS value: "
            << *rawValue << "\n";
        return 0.0;
    }
}

double readFixedFrameDeltaSecondsFromEnvironment(std::ostream& err) {
    return resolveFixedFrameDeltaSeconds(
        engine::env::get("PAC_FIXED_FRAME_DT_SECONDS"),
        err);
}

double clampFrameDeltaSeconds(double frameDt) {
    return std::min(frameDt, 0.25);
}

int dropExcessFixedTicks(double& accumulator, double timeStep) {
    if (accumulator < timeStep) {
        return 0;
    }

    const int dropped = static_cast<int>(std::floor(accumulator / timeStep));
    accumulator -= static_cast<double>(dropped) * timeStep;
    accumulator = std::max(0.0, accumulator);
    return dropped;
}

} // namespace game::runtime::loop_config

