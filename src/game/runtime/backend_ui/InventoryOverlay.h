#pragma once

#include "game/runtime/backend_ui/HudFormatting.h"

#include <cstddef>
#include <string>
#include <vector>

namespace game::runtime::ui_inventory {

struct OverlayRow {
    std::string itemId;
    std::string line;
    bool selected = false;
};

struct OverlayModel {
    std::vector<hud::InventoryEntry> visibleEntries;
    std::vector<OverlayRow> rows;
    int offset = 0;
    std::size_t totalCount = 0;
    std::size_t firstVisible = 0;
    std::size_t lastVisible = 0;
};

inline OverlayModel buildOverlayModel(const std::vector<hud::InventoryEntry>& allEntries,
                                      int requestedOffset,
                                      std::size_t visibleCount,
                                      const std::string& selectedItem) {
    OverlayModel out;
    out.totalCount = allEntries.size();
    out.offset = hud::clampInventoryOffset(requestedOffset, static_cast<int>(visibleCount), out.totalCount);
    out.visibleEntries = hud::sliceInventoryEntries(allEntries, out.offset, visibleCount);

    if (out.visibleEntries.empty()) {
        return out;
    }

    out.firstVisible = static_cast<std::size_t>(out.offset) + 1u;
    out.lastVisible = static_cast<std::size_t>(out.offset) + out.visibleEntries.size();
    out.rows.reserve(out.visibleEntries.size());

    for (std::size_t i = 0; i < out.visibleEntries.size(); ++i) {
        const auto& item = out.visibleEntries[i];
        OverlayRow row;
        row.itemId = item.id;
        row.selected = (item.id == selectedItem);
        row.line = hud::formatInventoryEntry(item);
        if (i < 9) {
            row.line = "[" + std::to_string(i + 1) + "] " + row.line;
        }
        if (row.selected) {
            row.line = "> " + row.line;
        }
        out.rows.push_back(std::move(row));
    }

    return out;
}

inline std::string makeTitleLabel(const OverlayModel& model) {
    if (model.totalCount == 0) return "Items [0/0]";
    return "Items [" + std::to_string(model.firstVisible) + "-" +
           std::to_string(model.lastVisible) + "/" +
           std::to_string(model.totalCount) + "]";
}

inline bool canScrollPrev(const OverlayModel& model) {
    return model.offset > 0;
}

inline bool canScrollNext(const OverlayModel& model) {
    return model.lastVisible < model.totalCount;
}

inline std::string prevPageLabel() {
    return "[Up/Left] Prev";
}

inline std::string nextPageLabel() {
    return "[Down/Right] Next";
}

inline std::string clearSelectionLabel() {
    return "[0] Clear selection";
}

inline std::string hintLabel() {
    return "Wheel scroll, arrows page, 1-9 select, 0 clear";
}

} // namespace game::runtime::ui_inventory




