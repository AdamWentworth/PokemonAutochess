#pragma once

#include "engine/input/InputEvent.h"
#include "game/runtime/session/SessionBackendInventoryUi.h"
#include "game/runtime/session/SessionLoopRuntime.h"

#include <cstddef>
#include <functional>

class CameraSystem;
class GameStateManager;
class GameWorld;
class UnitInteractionSystem;
struct EngineServices;
struct GameServices;

namespace game::ui {
struct UIViewport;
}

namespace LogBus {
class Logger;
}

namespace game::runtime::ui_inventory_panel {
struct PanelState;
}

namespace game::runtime::session_world_backdrop {
struct Route1BackdropTuningState;
}

namespace game::runtime::session_loop_bridge {

struct Context {
    LogBus::Logger* log = nullptr;
    session_loop_runtime::PauseState* pauseState = nullptr;
    EngineServices* engineServices = nullptr;
    game::ui::UIViewport* viewport = nullptr;
    UnitInteractionSystem* unitSystem = nullptr;
    CameraSystem* cameraSystem = nullptr;
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    GameServices* services = nullptr;
    ui_inventory_panel::PanelState* backendInventoryPanel = nullptr;
    std::size_t backendInventoryVisibleCount = 0u;
    bool renderWorldForInput = true;
    bool usesBackendGameUiPath = false;
    session_world_backdrop::Route1BackdropTuningState* route1BackdropTuning = nullptr;
    std::function<session_backend_inventory_ui::Dependencies()> inventoryDependencies;
    std::function<void()> saveDebugSnapshot;
    std::function<void()> loadDebugSnapshot;
};

struct FixedUpdateContext {
    session_loop_runtime::PauseState* pauseState = nullptr;
    EngineServices* engineServices = nullptr;
    bool usesBackendGameRenderPath = false;
    std::function<void(float)> advanceTime;
    std::function<void()> hydrateBackend;
    std::function<void(float)> tickUpdateGraph;
};

void handleEvent(const InputEvent& event, const Context& context);
void fixedUpdate(float dt, const FixedUpdateContext& context);

} // namespace game::runtime::session_loop_bridge
