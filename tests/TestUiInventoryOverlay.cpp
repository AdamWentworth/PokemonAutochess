#include "game/runtime/ui/InventoryOverlay.h"

#include <string>
#include <vector>

bool test_ui_inventory_overlay_contract(std::string& outFail) {
    using game::runtime::ui_inventory::OverlayModel;
    using game::runtime::ui_inventory::buildOverlayModel;
    using game::runtime::ui_inventory::canScrollNext;
    using game::runtime::ui_inventory::canScrollPrev;
    using game::runtime::ui_inventory::clearSelectionLabel;
    using game::runtime::ui_inventory::hintLabel;
    using game::runtime::ui_inventory::makeTitleLabel;
    using game::runtime::ui_inventory::nextPageLabel;
    using game::runtime::ui_inventory::prevPageLabel;
    using game::runtime::hud::InventoryEntry;

    const std::vector<InventoryEntry> all = {
        {"antidote", 5},
        {"potion", 4},
        {"pokeball", 3},
        {"burn_heal", 2},
        {"paralyze_heal", 1},
    };

    const OverlayModel model = buildOverlayModel(all, 1, 3, "pokeball");
    if (model.offset != 1) {
        outFail = "overlay offset should preserve in-range values";
        return false;
    }
    if (model.visibleEntries.size() != 3u || model.rows.size() != 3u) {
        outFail = "overlay visible row count mismatch";
        return false;
    }
    if (model.firstVisible != 2u || model.lastVisible != 4u) {
        outFail = "overlay visible index range mismatch";
        return false;
    }
    if (model.rows[0].itemId != "potion" || model.rows[0].line != "[1] Potion x4") {
        outFail = "overlay first row label mismatch";
        return false;
    }
    if (model.rows[1].itemId != "pokeball" || model.rows[1].line != "> [2] Pokeball x3") {
        outFail = "overlay selected row label mismatch";
        return false;
    }
    if (model.rows[2].itemId != "burn_heal" || model.rows[2].line != "[3] Burn Heal x2") {
        outFail = "overlay third row label mismatch";
        return false;
    }
    if (!canScrollPrev(model) || !canScrollNext(model)) {
        outFail = "overlay scroll availability should be true on middle page";
        return false;
    }
    if (makeTitleLabel(model) != "Items [2-4/5]") {
        outFail = "overlay title label mismatch";
        return false;
    }

    const OverlayModel clamped = buildOverlayModel(all, 999, 3, "");
    if (clamped.offset != 2) {
        outFail = "overlay offset should clamp to max";
        return false;
    }
    if (clamped.firstVisible != 3u || clamped.lastVisible != 5u) {
        outFail = "overlay clamped visible range mismatch";
        return false;
    }
    if (!canScrollPrev(clamped) || canScrollNext(clamped)) {
        outFail = "overlay scroll availability mismatch at tail page";
        return false;
    }

    const OverlayModel empty = buildOverlayModel({}, 4, 3, "");
    if (empty.offset != 0 || !empty.visibleEntries.empty() || !empty.rows.empty()) {
        outFail = "empty overlay model should have no visible rows and offset 0";
        return false;
    }
    if (makeTitleLabel(empty) != "Items [0/0]") {
        outFail = "empty overlay title mismatch";
        return false;
    }
    if (canScrollPrev(empty) || canScrollNext(empty)) {
        outFail = "empty overlay should not allow scrolling";
        return false;
    }

    if (prevPageLabel() != "[Up/Left] Prev") {
        outFail = "prev page label mismatch";
        return false;
    }
    if (nextPageLabel() != "[Down/Right] Next") {
        outFail = "next page label mismatch";
        return false;
    }
    if (clearSelectionLabel() != "[0] Clear selection") {
        outFail = "clear selection label mismatch";
        return false;
    }
    if (hintLabel() != "Wheel scroll, arrows page, 1-9 select, 0 clear") {
        outFail = "overlay hint label mismatch";
        return false;
    }

    return true;
}




