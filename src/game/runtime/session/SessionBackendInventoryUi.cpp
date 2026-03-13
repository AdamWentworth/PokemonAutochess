#include "game/runtime/session/SessionBackendInventoryUi.h"

#include "game/runtime/backend_ui/BackendHudFormatting.h"
#include "game/runtime/backend_ui/BackendInputSlots.h"

namespace game::runtime::session_backend_inventory_ui {

bool selectItem(const std::string& itemId, const Dependencies& deps) {
    if (itemId.empty() || !deps.getSelectedItem || !deps.setSelectedItem) return false;
    if (deps.getSelectedItem() == itemId) return true;
    deps.setSelectedItem(itemId);
    if (deps.logInfo) {
        deps.logInfo("Selected " + hud::humanizeToken(itemId) + ". Click a target.");
    }
    return true;
}

bool clearSelection(const Dependencies& deps) {
    if (!deps.getSelectedItem || !deps.setSelectedItem) return false;
    if (deps.getSelectedItem().empty()) return false;
    deps.setSelectedItem("");
    if (deps.logInfo) {
        deps.logInfo("Cleared selected item.");
    }
    return true;
}

void refreshPanel(backend_inventory_panel::PanelState& panel,
                  std::size_t visibleCount,
                  const Dependencies& deps) {
    if (!deps.listItems || !deps.getSelectedItem) {
        panel = {};
        return;
    }

    backend_inventory_panel::refreshPanelState(
        panel,
        deps.listItems(),
        visibleCount,
        deps.getSelectedItem());
}

bool applyOffsetDelta(backend_inventory_panel::PanelState& panel,
                      int delta,
                      std::size_t visibleCount,
                      const Dependencies& deps) {
    if (delta == 0 || !deps.getSelectedItem) return false;
    refreshPanel(panel, visibleCount, deps);
    return backend_inventory_panel::applyOffsetDelta(
        panel,
        delta,
        visibleCount,
        deps.getSelectedItem());
}

bool handleInput(backend_inventory_panel::PanelState& panel,
                 const InputEvent& event,
                 std::size_t visibleCount,
                 const Dependencies& deps) {
    if (event.type == InputEvent::Type::KeyDown && !event.repeat) {
        const int offsetDelta = backend_input::inventoryOffsetDeltaFromKey(
            event.keyId,
            static_cast<int>(visibleCount));
        if (applyOffsetDelta(panel, offsetDelta, visibleCount, deps)) {
            return true;
        }

        refreshPanel(panel, visibleCount, deps);
        const int slot = backend_input::slotFromNumberKey(event.keyId);
        const auto itemId = backend_inventory_panel::visibleItemForSlot(panel, slot);
        if (itemId && selectItem(*itemId, deps)) {
            return true;
        }
        return false;
    }

    if (event.type == InputEvent::Type::MouseWheel) {
        const int wheelDelta = backend_inventory_panel::offsetDeltaFromWheel(event.wheelY);
        return applyOffsetDelta(panel, wheelDelta, visibleCount, deps);
    }

    if (event.type != InputEvent::Type::MouseDown ||
        event.mouseButtonId != InputEvent::MouseButton::Left) {
        return false;
    }

    const auto* hit = backend_inventory_panel::findHit(
        panel,
        static_cast<float>(event.mouseX),
        static_cast<float>(event.mouseY));
    if (!hit) return false;

    if (hit->action == backend_inventory_panel::HitAction::ClearSelection) {
        return clearSelection(deps);
    }
    if (hit->action == backend_inventory_panel::HitAction::ScrollOffset) {
        return applyOffsetDelta(panel, hit->offsetDelta, visibleCount, deps);
    }
    return selectItem(hit->itemId, deps);
}

} // namespace game::runtime::session_backend_inventory_ui

