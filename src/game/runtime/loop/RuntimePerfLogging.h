#pragma once

#include <string>
#include <string_view>

#include "engine/core/EngineServices.h"

namespace game::runtime::perf_logging {

const char* terminalLogModeName(EngineTerminalLogMode mode);
EngineTerminalLogMode nextTerminalLogMode(EngineTerminalLogMode mode);

std::string formatTopFixedSystems(const EngineFixedPerfBreakdown& fixedBreakdown);

std::string formatPerfLine(const EngineFramePerfStats& framePerf);

std::string formatPerfJson(const EngineFramePerfStats& framePerf);

std::string formatPerfHitchLine(const EngineFramePerfStats& framePerf,
                                std::string_view reason = {});

std::string formatPerfHitchJson(const EngineFramePerfStats& framePerf,
                                std::string_view reason = {});

std::string formatGrowlDebugLine(const EngineGrowlDebugStats& growlDebug);

std::string formatGrowlDebugJson(const EngineGrowlDebugStats& growlDebug);

std::string formatScratchDebugLine(const EngineScratchDebugStats& scratchDebug,
                                   const EngineFramePerfStats& framePerf,
                                   std::string_view reason = {});

std::string formatScratchDebugJson(const EngineScratchDebugStats& scratchDebug,
                                   const EngineFramePerfStats& framePerf,
                                   std::string_view reason = {});

} // namespace game::runtime::perf_logging
