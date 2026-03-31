#pragma once

#include <string>

#include "engine/core/EngineServices.h"

namespace game::runtime::perf_logging {

const char* terminalLogModeName(EngineTerminalLogMode mode);
EngineTerminalLogMode nextTerminalLogMode(EngineTerminalLogMode mode);

std::string formatTopFixedSystems(const EngineFixedPerfBreakdown& fixedBreakdown);

std::string formatPerfLine(const EngineFramePerfStats& framePerf);

std::string formatPerfJson(const EngineFramePerfStats& framePerf);

std::string formatGrowlDebugLine(const EngineGrowlDebugStats& growlDebug);

std::string formatGrowlDebugJson(const EngineGrowlDebugStats& growlDebug);

} // namespace game::runtime::perf_logging
