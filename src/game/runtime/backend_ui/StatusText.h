#pragma once

#include "game/PhaseState.h"
#include "game/runtime/backend_ui/HudFormatting.h"

#include <algorithm>
#include <string>

namespace game::runtime::ui_status_text {

inline std::string roundPhaseLabel(RoundPhase phase) {
    switch (phase) {
        case RoundPhase::Planning: return "Planning";
        case RoundPhase::Battle: return "Battle";
        case RoundPhase::Resolution: return "Resolution";
        default: return "Planning";
    }
}

inline std::string modeLine(std::string mode) {
    if (mode.empty()) mode = "classic";
    return "Mode: " + mode;
}

inline std::string backendLine(const std::string& backend, const std::string& gpu) {
    return "Backend: " + backend + " | GPU: " + gpu;
}

inline std::string roundLine(RoundPhase phase, bool combatActive) {
    return "Round: " + roundPhaseLabel(phase) + " | Combat: " + (combatActive ? "active" : "idle");
}

inline std::string unitsLine(int playerAlive, int enemyAlive) {
    return "Units: Player " + std::to_string(std::max(0, playerAlive)) +
           " | Enemy " + std::to_string(std::max(0, enemyAlive));
}

inline std::string goldLine(int money) {
    return "Gold: " + std::to_string(std::max(0, money));
}

inline std::string selectedItemLine(const std::string& itemId) {
    return "Selected item: " + hud::humanizeToken(itemId);
}

} // namespace game::runtime::ui_status_text




