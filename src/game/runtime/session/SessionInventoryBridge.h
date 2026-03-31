#pragma once

#include "engine/input/InputEvent.h"
#include "game/runtime/session/SessionBackendInventoryUi.h"

#include <cstddef>

class GameWorld;

namespace LogBus {
class Logger;
}

namespace game::runtime::ui_inventory_panel {
struct PanelState;
}

namespace game::runtime::session_inventory_bridge {

struct Context {
    GameWorld* gameWorld = nullptr;
    LogBus::Logger* log = nullptr;
    ui_inventory_panel::PanelState* panel = nullptr;
    std::size_t visibleCount = 0u;
};

session_backend_inventory_ui::Dependencies makeDependencies(const Context& context);
void refreshPanelFromWorld(const Context& context);
bool clearSelection(const Context& context);
bool handleInput(const Context& context, const InputEvent& event);

} // namespace game::runtime::session_inventory_bridge
