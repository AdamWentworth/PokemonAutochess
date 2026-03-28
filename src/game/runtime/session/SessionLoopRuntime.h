#pragma once

#include "engine/input/InputEvent.h"

#include <functional>

namespace LogBus { class Logger; }
class GameStateManager;

namespace game::runtime::session_loop_runtime {

struct PauseState {
    bool paused = false;
    int stepTicks = 0;
};

struct InputOptions {
    LogBus::Logger* log = nullptr;
    bool renderWorldForInput = true;
    bool usesBackendGameUiPath = false;
    std::function<void(int, int)> onResize;
    std::function<void()> saveDebugSnapshot;
    std::function<void()> toggleBackdropTiles;
    std::function<void()> toggleTerminalLogMode;
    std::function<void()> loadDebugSnapshot;
    std::function<void()> openMainMenu;
    std::function<bool()> clearSelection;
    std::function<bool(const InputEvent&)> handleInventoryInput;
    std::function<void(const InputEvent&)> handleCameraInput;
    std::function<void(const InputEvent&)> handleUnitInput;
    std::function<void(const InputEvent&)> handleStateInput;
};

struct FixedUpdateOptions {
    bool usesBackendGameRenderPath = false;
    std::function<void(float)> advanceTime;
    std::function<void()> hydrateBackend;
    std::function<void(float)> addBackendHydrateMs;
    std::function<void(float)> tickUpdateGraph;
};

bool renderWorldForInput(GameStateManager* stateManager);

void handleEvent(const InputEvent& event,
                 PauseState& pauseState,
                 const InputOptions& options);

void fixedUpdate(float dt,
                 PauseState& pauseState,
                 const FixedUpdateOptions& options);

} // namespace game::runtime::session_loop_runtime
