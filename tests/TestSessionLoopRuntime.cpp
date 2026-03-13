#include "game/runtime/session/SessionLoopRuntime.h"

#include <string>

bool test_session_loop_runtime_contract(std::string& outFail) {
    using game::runtime::session_loop_runtime::FixedUpdateOptions;
    using game::runtime::session_loop_runtime::InputOptions;
    using game::runtime::session_loop_runtime::PauseState;

    {
        PauseState pauseState;
        int saveCount = 0;
        int loadCount = 0;
        int menuCount = 0;
        int stateInputCount = 0;

        InputOptions options;
        options.renderWorldForInput = true;
        options.saveDebugSnapshot = [&]() { ++saveCount; };
        options.loadDebugSnapshot = [&]() { ++loadCount; };
        options.openMainMenu = [&]() { ++menuCount; };
        options.handleStateInput = [&](const InputEvent&) { ++stateInputCount; };

        game::runtime::session_loop_runtime::handleEvent(
            InputEvent::KeyDownEvent(InputEvent::Key::P),
            pauseState,
            options);
        if (!pauseState.paused || pauseState.stepTicks != 0 || stateInputCount != 0) {
            outFail = "SessionLoopRuntime should toggle dev pause and consume the pause hotkey.";
            return false;
        }

        game::runtime::session_loop_runtime::handleEvent(
            InputEvent::KeyDownEvent(InputEvent::Key::O),
            pauseState,
            options);
        if (pauseState.stepTicks != 1 || stateInputCount != 0) {
            outFail = "SessionLoopRuntime should arm one paused fixed-step on the step hotkey.";
            return false;
        }

        game::runtime::session_loop_runtime::handleEvent(
            InputEvent::KeyDownEvent(InputEvent::Key::F5),
            pauseState,
            options);
        game::runtime::session_loop_runtime::handleEvent(
            InputEvent::KeyDownEvent(InputEvent::Key::F9),
            pauseState,
            options);
        game::runtime::session_loop_runtime::handleEvent(
            InputEvent::KeyDownEvent(InputEvent::Key::Escape),
            pauseState,
            options);
        if (saveCount != 1 || loadCount != 1 || menuCount != 1) {
            outFail = "SessionLoopRuntime should route snapshot and escape hotkeys through session callbacks.";
            return false;
        }
    }

    {
        PauseState pauseState;
        int clearCount = 0;
        int inventoryCount = 0;
        int cameraCount = 0;
        int unitCount = 0;
        int stateCount = 0;
        int resizeWidth = 0;
        int resizeHeight = 0;

        InputOptions options;
        options.renderWorldForInput = true;
        options.usesBackendGameUiPath = true;
        options.onResize = [&](int w, int h) {
            resizeWidth = w;
            resizeHeight = h;
        };
        options.clearSelection = [&]() {
            ++clearCount;
            return true;
        };
        options.handleInventoryInput = [&](const InputEvent&) {
            ++inventoryCount;
            return true;
        };
        options.handleCameraInput = [&](const InputEvent&) { ++cameraCount; };
        options.handleUnitInput = [&](const InputEvent&) { ++unitCount; };
        options.handleStateInput = [&](const InputEvent&) { ++stateCount; };

        game::runtime::session_loop_runtime::handleEvent(
            InputEvent::ResizeEvent(1280, 720, 1920, 1080),
            pauseState,
            options);
        if (resizeWidth != 1920 || resizeHeight != 1080) {
            outFail = "SessionLoopRuntime should forward resize drawable dimensions through the resize callback.";
            return false;
        }
        clearCount = 0;
        inventoryCount = 0;
        cameraCount = 0;
        unitCount = 0;
        stateCount = 0;

        game::runtime::session_loop_runtime::handleEvent(
            InputEvent::KeyDownEvent(InputEvent::Key::Num0),
            pauseState,
            options);
        if (clearCount != 1 || inventoryCount != 0 || cameraCount != 0 || unitCount != 0 || stateCount != 0) {
            outFail = "SessionLoopRuntime should consume the clear-selection hotkey before gameplay handlers.";
            return false;
        }

        game::runtime::session_loop_runtime::handleEvent(
            InputEvent::MouseWheelEvent(0, -1),
            pauseState,
            options);
        if (inventoryCount != 1 || cameraCount != 0 || unitCount != 0 || stateCount != 0) {
            outFail = "SessionLoopRuntime should consume backend inventory input before gameplay handlers.";
            return false;
        }

        options.handleInventoryInput = [&](const InputEvent&) {
            ++inventoryCount;
            return false;
        };
        game::runtime::session_loop_runtime::handleEvent(
            InputEvent::MouseMoveEvent(100, 100),
            pauseState,
            options);
        if (cameraCount != 1 || unitCount != 1 || stateCount != 1) {
            outFail = "SessionLoopRuntime should forward non-consumed input to camera, unit, and state handlers.";
            return false;
        }
    }

    {
        PauseState pauseState;
        pauseState.paused = true;
        pauseState.stepTicks = 0;
        int advanceCount = 0;
        int hydrateCount = 0;
        int tickCount = 0;

        FixedUpdateOptions options;
        options.usesBackendGameRenderPath = true;
        options.advanceTime = [&](float) { ++advanceCount; };
        options.hydrateBackend = [&]() { ++hydrateCount; };
        options.tickUpdateGraph = [&](float) { ++tickCount; };

        game::runtime::session_loop_runtime::fixedUpdate(0.016f, pauseState, options);
        if (advanceCount != 0 || hydrateCount != 0 || tickCount != 0) {
            outFail = "SessionLoopRuntime should skip fixed update work while paused with no step ticks.";
            return false;
        }
    }

    {
        PauseState pauseState;
        pauseState.paused = true;
        pauseState.stepTicks = 1;
        int advanceCount = 0;
        int hydrateCount = 0;
        int hydrateMsCount = 0;
        int tickCount = 0;

        FixedUpdateOptions options;
        options.usesBackendGameRenderPath = true;
        options.advanceTime = [&](float) { ++advanceCount; };
        options.hydrateBackend = [&]() { ++hydrateCount; };
        options.addBackendHydrateMs = [&](float ms) {
            if (ms >= 0.0f) ++hydrateMsCount;
        };
        options.tickUpdateGraph = [&](float) { ++tickCount; };

        game::runtime::session_loop_runtime::fixedUpdate(0.016f, pauseState, options);
        if (advanceCount != 1 || hydrateCount != 1 || hydrateMsCount != 1 || tickCount != 1 || pauseState.stepTicks != 0) {
            outFail = "SessionLoopRuntime should execute one fixed step, record hydrate timing, and consume the pause step tick.";
            return false;
        }
    }

    return true;
}
