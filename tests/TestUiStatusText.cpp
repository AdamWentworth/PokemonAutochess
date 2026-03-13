#include "game/runtime/ui/StatusText.h"

#include <string>

bool test_ui_status_text_contract(std::string& outFail) {
    using game::runtime::ui_status_text::backendLine;
    using game::runtime::ui_status_text::goldLine;
    using game::runtime::ui_status_text::modeLine;
    using game::runtime::ui_status_text::roundLine;
    using game::runtime::ui_status_text::roundPhaseLabel;
    using game::runtime::ui_status_text::selectedItemLine;
    using game::runtime::ui_status_text::unitsLine;

    if (roundPhaseLabel(RoundPhase::Planning) != "Planning") {
        outFail = "round phase label mismatch for planning";
        return false;
    }
    if (roundPhaseLabel(RoundPhase::Battle) != "Battle") {
        outFail = "round phase label mismatch for battle";
        return false;
    }
    if (roundPhaseLabel(RoundPhase::Resolution) != "Resolution") {
        outFail = "round phase label mismatch for resolution";
        return false;
    }
    if (modeLine("adventure") != "Mode: adventure") {
        outFail = "mode line mismatch";
        return false;
    }
    if (modeLine("") != "Mode: classic") {
        outFail = "empty mode should fallback to classic";
        return false;
    }
    if (backendLine("d3d12", "NVIDIA") != "Backend: d3d12 | GPU: NVIDIA") {
        outFail = "backend line mismatch";
        return false;
    }
    if (roundLine(RoundPhase::Battle, true) != "Round: Battle | Combat: active") {
        outFail = "round line mismatch for active combat";
        return false;
    }
    if (roundLine(RoundPhase::Planning, false) != "Round: Planning | Combat: idle") {
        outFail = "round line mismatch for idle combat";
        return false;
    }
    if (unitsLine(3, 4) != "Units: Player 3 | Enemy 4") {
        outFail = "units line mismatch";
        return false;
    }
    if (unitsLine(-2, -7) != "Units: Player 0 | Enemy 0") {
        outFail = "units line should clamp negatives";
        return false;
    }
    if (goldLine(9) != "Gold: 9") {
        outFail = "gold line mismatch";
        return false;
    }
    if (goldLine(-3) != "Gold: 0") {
        outFail = "gold line should clamp negatives";
        return false;
    }
    if (selectedItemLine("super_potion") != "Selected item: Super Potion") {
        outFail = "selected item line mismatch";
        return false;
    }

    return true;
}




