#pragma once

#include "engine/core/EngineServices.h"
#include "game/logging/LoggerUtil.h"

#include <cctype>
#include <string>
#include <string_view>

namespace game::scratch_trace {

inline bool isTerminalModeEnabled(const EngineServices* services) {
    return services && services->terminalLogMode == EngineTerminalLogMode::ScratchVfx;
}

inline bool isScratchMove(std::string_view moveName) {
    constexpr std::string_view kScratch = "scratch";
    if (moveName.size() != kScratch.size()) return false;
    for (std::size_t i = 0; i < kScratch.size(); ++i) {
        const char lhs =
            static_cast<char>(std::tolower(static_cast<unsigned char>(moveName[i])));
        if (lhs != kScratch[i]) return false;
    }
    return true;
}

inline bool shouldTrace(const EngineServices* services, std::string_view moveName) {
    return isTerminalModeEnabled(services) && isScratchMove(moveName);
}

inline void emit(LogBus::Logger* logger,
                 std::string_view stage,
                 const std::string& details) {
    game::log::infoTerminalOnly(
        logger,
        std::string("[ScratchTrace] stage=") + std::string(stage) + " " + details);
}

} // namespace game::scratch_trace
