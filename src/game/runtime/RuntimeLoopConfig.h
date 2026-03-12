#pragma once

#include <iosfwd>
#include <optional>
#include <string>

namespace game::runtime::loop_config {

int resolveMaxFixedTicksPerFrame(const std::optional<std::string>& rawValue, std::ostream& err);

int readMaxFixedTicksPerFrameFromEnvironment(std::ostream& err);

double clampFrameDeltaSeconds(double frameDt);

int dropExcessFixedTicks(double& accumulator, double timeStep);

} // namespace game::runtime::loop_config
