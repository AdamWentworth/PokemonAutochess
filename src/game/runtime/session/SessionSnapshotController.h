#pragma once

#include <cstddef>
#include <functional>
#include <string>

class IRenderBackend;
class GameStateManager;
class GameWorld;
class RoundSystem;
struct GameServices;

namespace LogBus {
class Logger;
}

namespace engine::log {
class Sink;
}

namespace game::ui {
struct UIViewport;
}

namespace game::runtime::session_snapshot_controller {

struct Context {
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    GameServices* services = nullptr;
    RoundSystem* roundSystem = nullptr;
    LogBus::Logger* log = nullptr;
    engine::log::Sink* consoleLog = nullptr;
    IRenderBackend* renderer = nullptr;
    game::ui::UIViewport* viewport = nullptr;
    std::function<void()> refreshInventoryPanel;
    std::function<void()> resetRenderCaches;
    std::function<bool()> usesBackendGameRenderPath;
    std::function<std::size_t(int, int, bool)> prewarmWorldIndexedLayer;
};

void saveDebugStateSnapshot(const std::string& path, const Context& context);
void loadDebugStateSnapshot(const std::string& path, const Context& context);
void maybeAutoLoadDebugStateSnapshot(const std::string& path,
                                     bool autoLoadEnabled,
                                     const Context& context);

} // namespace game::runtime::session_snapshot_controller
