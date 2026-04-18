#pragma once

#include "engine/core/EngineServices.h"
#include "game/logging/LoggerUtil.h"

#include <string>
#include <string_view>

namespace game::combat_decision_trace {

inline bool isTerminalModeEnabled(const EngineServices* services) {
    return services && services->terminalLogMode == EngineTerminalLogMode::CombatDecision;
}

inline void emit(LogBus::Logger* logger,
                 std::string_view stage,
                 const std::string& details) {
    game::log::infoTerminalOnly(
        logger,
        std::string("[CombatDecisionTrace] stage=") + std::string(stage) + " " + details);
}

} // namespace game::combat_decision_trace
