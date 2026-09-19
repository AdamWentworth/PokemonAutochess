#pragma once

#include "game/runtime/GameRuntimeServices.h"
#include "game/logging/LoggerUtil.h"

#include <string>
#include <string_view>

namespace game::combat_decision_trace {

inline bool isTerminalModeEnabled(const GameRuntimeServices *services) {
    return services && services->terminalLogMode == GameTerminalLogMode::CombatDecision;
}

inline void emit(LogBus::Logger* logger,
                 std::string_view stage,
                 const std::string& details) {
    game::log::infoTerminalOnly(
        logger,
        std::string("[CombatDecisionTrace] stage=") + std::string(stage) + " " + details);
}

} // namespace game::combat_decision_trace
