#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "engine/input/InputEvent.h"
#include "game/runtime/backend_ui/BackendInventoryPanel.h"

namespace game::runtime::session_backend_inventory_ui {

struct Dependencies {
    std::function<std::string()> getSelectedItem;
    std::function<void(const std::string&)> setSelectedItem;
    std::function<std::vector<std::pair<std::string, int>>()> listItems;
    std::function<void(const std::string&)> logInfo;
};

bool selectItem(const std::string& itemId, const Dependencies& deps);
bool clearSelection(const Dependencies& deps);
void refreshPanel(backend_inventory_panel::PanelState& panel,
                  std::size_t visibleCount,
                  const Dependencies& deps);
bool applyOffsetDelta(backend_inventory_panel::PanelState& panel,
                      int delta,
                      std::size_t visibleCount,
                      const Dependencies& deps);
bool handleInput(backend_inventory_panel::PanelState& panel,
                 const InputEvent& event,
                 std::size_t visibleCount,
                 const Dependencies& deps);

} // namespace game::runtime::session_backend_inventory_ui

