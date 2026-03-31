#include "game/runtime/session/SessionInventoryBridge.h"

#include "game/GameWorld.h"
#include "game/logging/LogBus.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace game::runtime::session_inventory_bridge {

session_backend_inventory_ui::Dependencies makeDependencies(const Context& context) {
    return session_backend_inventory_ui::Dependencies{
        .getSelectedItem =
            [gameWorld = context.gameWorld]() -> std::string {
                return gameWorld ? gameWorld->getSelectedItem() : std::string{};
            },
        .setSelectedItem =
            [gameWorld = context.gameWorld](const std::string& itemId) {
                if (gameWorld) {
                    gameWorld->setSelectedItem(itemId);
                }
            },
        .listItems =
            [gameWorld = context.gameWorld]() -> std::vector<std::pair<std::string, int>> {
                return gameWorld ? gameWorld->listItems()
                                 : std::vector<std::pair<std::string, int>>{};
            },
        .getInventoryRevision =
            [gameWorld = context.gameWorld]() -> std::uint64_t {
                return gameWorld ? gameWorld->getInventoryUiRevision() : 0u;
            },
        .logInfo =
            [log = context.log](const std::string& message) {
                if (log) {
                    log->catchInfo(message);
                }
            },
    };
}

void refreshPanelFromWorld(const Context& context) {
    if (!context.panel) return;
    session_backend_inventory_ui::refreshPanel(
        *context.panel,
        context.visibleCount,
        makeDependencies(context));
}

bool clearSelection(const Context& context) {
    return session_backend_inventory_ui::clearSelection(makeDependencies(context));
}

bool handleInput(const Context& context, const InputEvent& event) {
    if (!context.panel) return false;
    return session_backend_inventory_ui::handleInput(
        *context.panel,
        event,
        context.visibleCount,
        makeDependencies(context));
}

} // namespace game::runtime::session_inventory_bridge
