#pragma once

#include <string>

#include "engine/core/EngineServices.h"

namespace game::runtime::perf_logging {

std::string formatTopFixedSystems(const EngineFixedPerfBreakdown& fixedBreakdown);

std::string formatPerfLine(const EngineFramePerfStats& framePerf);

std::string formatPerfJson(const EngineFramePerfStats& framePerf);

} // namespace game::runtime::perf_logging
