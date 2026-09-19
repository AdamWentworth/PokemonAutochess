#pragma once

#include <string>
#include <string_view>

#include "game/runtime/GameRuntimeServices.h"

namespace game::runtime::perf_logging {

const char* terminalLogModeName(GameTerminalLogMode mode);
GameTerminalLogMode nextTerminalLogMode(GameTerminalLogMode mode);

std::string formatTopFixedSystems(const GameFixedPerfBreakdown& fixedBreakdown);

std::string formatPerfLine(const GameFramePerfStats& framePerf);

std::string formatPerfJson(const GameFramePerfStats& framePerf);

std::string formatPerfHitchLine(const GameFramePerfStats& framePerf,
                                std::string_view reason = {});

std::string formatPerfHitchJson(const GameFramePerfStats& framePerf,
                                std::string_view reason = {});

std::string formatGrowlDebugLine(const GameGrowlDebugStats& growlDebug);

std::string formatGrowlDebugJson(const GameGrowlDebugStats& growlDebug);

std::string formatScratchDebugLine(const GameScratchDebugStats& scratchDebug,
                                   const GameFramePerfStats& framePerf,
                                   std::string_view reason = {});

std::string formatScratchDebugJson(const GameScratchDebugStats& scratchDebug,
                                   const GameFramePerfStats& framePerf,
                                   std::string_view reason = {});

} // namespace game::runtime::perf_logging
