#pragma once

#include "game/runtime/BackendHudFormatting.h"
#include "game/runtime/BackendInventoryOverlay.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace game::runtime::backend_inventory_panel {

enum class HitAction {
    SelectItem,
    ClearSelection,
    ScrollOffset,
};

struct HitRegion {
    HitAction action = HitAction::SelectItem;
    std::string itemId;
    int offsetDelta = 0;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct PanelState {
    int offset = 0;
    std::vector<hud::InventoryEntry> allEntries;
    backend_inventory::OverlayModel model;
    std::vector<HitRegion> hitRegions;
};

inline int offsetDeltaFromWheel(int wheelY) {
    if (wheelY > 0) return -1;
    if (wheelY < 0) return 1;
    return 0;
}

inline void clearHitRegions(PanelState& panel) {
    panel.hitRegions.clear();
}

inline void refreshPanelState(PanelState& panel,
                              const std::vector<std::pair<std::string, int>>& rawItems,
                              std::size_t visibleCount,
                              const std::string& selectedItem) {
    panel.allEntries = hud::normalizeInventoryEntries(rawItems, 0);
    panel.model = backend_inventory::buildOverlayModel(
        panel.allEntries,
        panel.offset,
        visibleCount,
        selectedItem);
    panel.offset = panel.model.offset;
    clearHitRegions(panel);
}

inline bool applyOffsetDelta(PanelState& panel,
                             int delta,
                             std::size_t visibleCount,
                             const std::string& selectedItem) {
    if (delta == 0) return false;

    const int nextOffset = hud::clampInventoryOffset(
        panel.offset + delta,
        static_cast<int>(visibleCount),
        panel.allEntries.size());
    if (nextOffset == panel.offset) return false;

    panel.offset = nextOffset;
    panel.model = backend_inventory::buildOverlayModel(
        panel.allEntries,
        panel.offset,
        visibleCount,
        selectedItem);
    panel.offset = panel.model.offset;
    clearHitRegions(panel);
    return true;
}

inline std::optional<std::string> visibleItemForSlot(const PanelState& panel, int slot) {
    if (slot <= 0) return std::nullopt;
    const std::size_t index = static_cast<std::size_t>(slot - 1);
    if (index >= panel.model.visibleEntries.size()) return std::nullopt;
    return panel.model.visibleEntries[index].id;
}

inline const HitRegion* findHit(const PanelState& panel, float x, float y) {
    for (const auto& hit : panel.hitRegions) {
        const bool insideX = x >= hit.x && x <= (hit.x + hit.w);
        const bool insideY = y >= hit.y && y <= (hit.y + hit.h);
        if (insideX && insideY) return &hit;
    }
    return nullptr;
}

} // namespace game::runtime::backend_inventory_panel

