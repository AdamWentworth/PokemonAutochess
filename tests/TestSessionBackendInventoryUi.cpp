#include <string>
#include <utility>
#include <vector>

#include "game/runtime/session/SessionBackendInventoryUi.h"

bool test_session_backend_inventory_ui_contract(std::string& outFail) {
    using game::runtime::ui_inventory_panel::HitAction;
    using game::runtime::ui_inventory_panel::HitRegion;
    using game::runtime::ui_inventory_panel::PanelState;
    using game::runtime::session_backend_inventory_ui::Dependencies;

    std::vector<std::pair<std::string, int>> items{
        {"potion", 2},
        {"rare_candy", 1},
        {"ether", 3},
    };
    std::string selectedItem;
    std::vector<std::string> infoLog;
    PanelState panel;

    const Dependencies deps{
        .getSelectedItem = [&]() { return selectedItem; },
        .setSelectedItem = [&](const std::string& itemId) { selectedItem = itemId; },
        .listItems = [&]() { return items; },
        .logInfo = [&](const std::string& message) { infoLog.push_back(message); },
    };

    game::runtime::session_backend_inventory_ui::refreshPanel(panel, 2u, deps);
    if (panel.model.visibleEntries.size() != 2u || panel.offset != 0) {
        outFail = "SessionBackendInventoryUi should build the visible panel window from world items.";
        return false;
    }
    const auto visibleSlot2 = game::runtime::ui_inventory_panel::visibleItemForSlot(panel, 2);
    if (!visibleSlot2) {
        outFail = "SessionBackendInventoryUi test setup expected a visible item in slot 2.";
        return false;
    }

    InputEvent numberSelect;
    numberSelect.type = InputEvent::Type::KeyDown;
    numberSelect.keyId = InputEvent::Key::Num2;
    if (!game::runtime::session_backend_inventory_ui::handleInput(panel, numberSelect, 2u, deps) ||
        selectedItem != *visibleSlot2 ||
        infoLog.empty()) {
        outFail = "SessionBackendInventoryUi should select the visible item for number-key input.";
        return false;
    }

    selectedItem.clear();
    infoLog.clear();
    InputEvent wheelDown;
    wheelDown.type = InputEvent::Type::MouseWheel;
    wheelDown.wheelY = -1;
    if (!game::runtime::session_backend_inventory_ui::handleInput(panel, wheelDown, 2u, deps) ||
        panel.offset != 1) {
        outFail = "SessionBackendInventoryUi should page the visible inventory window on mouse-wheel input.";
        return false;
    }

    selectedItem = "potion";
    panel.hitRegions = {
        HitRegion{
            .action = HitAction::ClearSelection,
            .x = 10.0f,
            .y = 10.0f,
            .w = 30.0f,
            .h = 20.0f,
        },
    };
    InputEvent mouseClear;
    mouseClear.type = InputEvent::Type::MouseDown;
    mouseClear.mouseButtonId = InputEvent::MouseButton::Left;
    mouseClear.mouseX = 15;
    mouseClear.mouseY = 15;
    if (!game::runtime::session_backend_inventory_ui::handleInput(panel, mouseClear, 2u, deps) ||
        !selectedItem.empty()) {
        outFail = "SessionBackendInventoryUi should clear the selected item when the clear-hit region is clicked.";
        return false;
    }

    return true;
}

