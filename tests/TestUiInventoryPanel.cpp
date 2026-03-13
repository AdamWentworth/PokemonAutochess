#include "game/runtime/backend_ui/InventoryPanel.h"

#include <string>
#include <vector>

bool test_ui_inventory_panel_contract(std::string& outFail) {
    using game::runtime::ui_inventory_panel::HitAction;
    using game::runtime::ui_inventory_panel::HitRegion;
    using game::runtime::ui_inventory_panel::PanelState;
    using game::runtime::ui_inventory_panel::applyOffsetDelta;
    using game::runtime::ui_inventory_panel::findHit;
    using game::runtime::ui_inventory_panel::offsetDeltaFromWheel;
    using game::runtime::ui_inventory_panel::refreshPanelState;
    using game::runtime::ui_inventory_panel::visibleItemForSlot;

    if (offsetDeltaFromWheel(1) != -1) {
        outFail = "wheel up should map to -1 offset delta";
        return false;
    }
    if (offsetDeltaFromWheel(-1) != 1) {
        outFail = "wheel down should map to +1 offset delta";
        return false;
    }
    if (offsetDeltaFromWheel(0) != 0) {
        outFail = "zero wheel delta should map to zero offset delta";
        return false;
    }

    const std::vector<std::pair<std::string, int>> raw = {
        {"pokeball", 5},
        {"potion", 4},
        {"antidote", 3},
        {"burn_heal", 2},
        {"paralyze_heal", 1},
    };

    PanelState panel;
    refreshPanelState(panel, raw, 3, "potion");
    if (panel.offset != 0) {
        outFail = "initial refresh should keep offset at zero";
        return false;
    }
    if (panel.model.totalCount != 5u || panel.model.visibleEntries.size() != 3u) {
        outFail = "panel refresh visible counts mismatch";
        return false;
    }
    if (panel.model.rows[1].line != "> [2] Potion x4") {
        outFail = "selected row prefix mismatch";
        return false;
    }

    if (visibleItemForSlot(panel, 1).value_or("") != "pokeball") {
        outFail = "slot 1 item mapping mismatch";
        return false;
    }
    if (visibleItemForSlot(panel, 3).value_or("") != "antidote") {
        outFail = "slot 3 item mapping mismatch";
        return false;
    }
    if (visibleItemForSlot(panel, 4).has_value()) {
        outFail = "out-of-range slot should not map to an item";
        return false;
    }

    if (!applyOffsetDelta(panel, 1, 3, "potion")) {
        outFail = "offset +1 should be applied when scroll range exists";
        return false;
    }
    if (panel.offset != 1) {
        outFail = "offset should update to 1";
        return false;
    }
    if (panel.model.firstVisible != 2u || panel.model.lastVisible != 4u) {
        outFail = "visible range after +1 mismatch";
        return false;
    }
    if (!applyOffsetDelta(panel, 99, 3, "potion")) {
        outFail = "large positive offset delta should clamp to max and apply once";
        return false;
    }
    if (panel.offset != 2) {
        outFail = "offset should clamp to max value 2";
        return false;
    }
    if (applyOffsetDelta(panel, 1, 3, "potion")) {
        outFail = "offset delta should no-op after already at max";
        return false;
    }

    panel.hitRegions.push_back(HitRegion{HitAction::ScrollOffset, "", -1, 10.0f, 10.0f, 20.0f, 10.0f});
    panel.hitRegions.push_back(HitRegion{HitAction::SelectItem, "potion", 0, 40.0f, 40.0f, 20.0f, 10.0f});
    const auto* first = findHit(panel, 15.0f, 15.0f);
    if (!first || first->action != HitAction::ScrollOffset || first->offsetDelta != -1) {
        outFail = "hit lookup mismatch for scroll region";
        return false;
    }
    const auto* second = findHit(panel, 45.0f, 45.0f);
    if (!second || second->action != HitAction::SelectItem || second->itemId != "potion") {
        outFail = "hit lookup mismatch for select region";
        return false;
    }
    if (findHit(panel, 5.0f, 5.0f) != nullptr) {
        outFail = "hit lookup should return null for miss";
        return false;
    }

    return true;
}



