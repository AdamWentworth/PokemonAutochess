#include "game/runtime/session/SessionLoopRuntime.h"

#include "game/GameStateManager.h"
#include "game/logging/LoggerUtil.h"
#include "game/runtime/ui/InputSlots.h"

#include <chrono>

namespace game::runtime::session_loop_runtime {

bool renderWorldForInput(GameStateManager* stateManager) {
    if (!stateManager) return true;
    if (auto* state = stateManager->getCurrentState()) {
        return state->shouldRenderWorld();
    }
    return true;
}

void handleEvent(const InputEvent& event,
                 PauseState& pauseState,
                 const InputOptions& options) {
    if (event.type == InputEvent::Type::Resize) {
        if (options.onResize) {
            options.onResize(event.drawableW, event.drawableH);
        }
    }

    if (event.type == InputEvent::Type::KeyDown && !event.repeat) {
        if (event.keyId == InputEvent::Key::P) {
            pauseState.paused = !pauseState.paused;
            pauseState.stepTicks = 0;
            game::log::info(
                options.log,
                pauseState.paused
                    ? "[DevPause] ON (P resumes, O steps one frame)"
                    : "[DevPause] OFF");
            return;
        }
        if (event.keyId == InputEvent::Key::O && pauseState.paused) {
            pauseState.stepTicks = 1;
            game::log::info(options.log, "[DevPause] Step 1 frame");
            return;
        }
        if (event.keyId == InputEvent::Key::F5) {
            if (options.saveDebugSnapshot) options.saveDebugSnapshot();
            return;
        }
        if (event.keyId == InputEvent::Key::F9) {
            if (options.loadDebugSnapshot) options.loadDebugSnapshot();
            return;
        }
    }

    if (event.type == InputEvent::Type::KeyDown &&
        event.keyId == InputEvent::Key::Escape &&
        !event.repeat) {
        if (options.renderWorldForInput && options.openMainMenu) {
            options.openMainMenu();
            return;
        }
    }

    if (options.renderWorldForInput &&
        event.type == InputEvent::Type::KeyDown &&
        !event.repeat &&
        runtime::ui_input::isClearSelectionKey(event.keyId)) {
        if (options.clearSelection && options.clearSelection()) {
            return;
        }
    }

    if (options.renderWorldForInput && options.usesBackendGameUiPath) {
        if (options.handleInventoryInput && options.handleInventoryInput(event)) {
            return;
        }
    }

    if (options.renderWorldForInput && options.handleCameraInput) {
        options.handleCameraInput(event);
    }
    if (options.renderWorldForInput && options.handleUnitInput) {
        options.handleUnitInput(event);
    }
    if (options.handleStateInput) {
        options.handleStateInput(event);
    }
}

void fixedUpdate(float dt,
                 PauseState& pauseState,
                 const FixedUpdateOptions& options) {
    if (pauseState.paused && pauseState.stepTicks <= 0) {
        return;
    }

    if (options.advanceTime) {
        options.advanceTime(dt);
    }

    if (options.usesBackendGameRenderPath && options.hydrateBackend) {
        const auto hydrateStart = std::chrono::high_resolution_clock::now();
        options.hydrateBackend();
        if (options.addBackendHydrateMs) {
            const auto hydrateEnd = std::chrono::high_resolution_clock::now();
            options.addBackendHydrateMs(static_cast<float>(
                std::chrono::duration<double, std::milli>(hydrateEnd - hydrateStart).count()));
        }
    }

    if (options.tickUpdateGraph) {
        options.tickUpdateGraph(dt);
    }

    if (pauseState.paused && pauseState.stepTicks > 0) {
        --pauseState.stepTicks;
    }
}

} // namespace game::runtime::session_loop_runtime
