#include "game/runtime/session/SessionLoopBridge.h"

#include "engine/core/EngineServices.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/GameStateManager.h"
#include "game/GameWorld.h"
#include "game/logging/LoggerUtil.h"
#include "game/runtime/loop/RuntimePerfLogging.h"
#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/session/SessionWorldBackdrop.h"
#include "game/state/scripted/ScriptedState.h"
#include "game/systems/CameraSystem.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/ui/UIViewport.h"

#include <algorithm>
#include <memory>
#include <string>

namespace game::runtime::session_loop_bridge {

namespace {

constexpr float kRoute1BackdropMoveStepCells = 0.25f;
constexpr float kRoute1BackdropLiftStep = 0.10f;
constexpr float kRoute1BackdropScaleStep = 0.25f;
constexpr float kRoute1BackdropYawStepDeg = 5.0f;

void logRoute1BackdropTuningControls(LogBus::Logger* log) {
    game::log::infoTerminalOnly(
        log,
        "[Backdrop][Route1Tune] Controls: F8 toggle | Arrows move X/Z | Q/E move Y | Z/X scale | A/D yaw | R reset");
}

void logRoute1BackdropTuningState(
    LogBus::Logger* log,
    const session_world_backdrop::Route1BackdropTuningState& state) {
    game::log::infoTerminalOnly(
        log,
        session_world_backdrop::formatRoute1BackdropTuningState(state));
}

bool handleRoute1BackdropTuningInput(
    const InputEvent& inputEvent,
    LogBus::Logger* log,
    session_world_backdrop::Route1BackdropTuningState* state) {
    if (!state || !state->enabled || inputEvent.type != InputEvent::Type::KeyDown) {
        return false;
    }

    bool changed = false;
    switch (inputEvent.keyId) {
        case InputEvent::Key::Left:
            state->offsetXCells -= kRoute1BackdropMoveStepCells;
            changed = true;
            break;
        case InputEvent::Key::Right:
            state->offsetXCells += kRoute1BackdropMoveStepCells;
            changed = true;
            break;
        case InputEvent::Key::Up:
            state->offsetZCells -= kRoute1BackdropMoveStepCells;
            changed = true;
            break;
        case InputEvent::Key::Down:
            state->offsetZCells += kRoute1BackdropMoveStepCells;
            changed = true;
            break;
        case InputEvent::Key::Q:
            state->offsetY -= kRoute1BackdropLiftStep;
            changed = true;
            break;
        case InputEvent::Key::E:
            state->offsetY += kRoute1BackdropLiftStep;
            changed = true;
            break;
        case InputEvent::Key::Z:
            state->scaleMul = std::max(0.1f, state->scaleMul - kRoute1BackdropScaleStep);
            changed = true;
            break;
        case InputEvent::Key::X:
            state->scaleMul += kRoute1BackdropScaleStep;
            changed = true;
            break;
        case InputEvent::Key::A:
            state->yawDeg -= kRoute1BackdropYawStepDeg;
            changed = true;
            break;
        case InputEvent::Key::D:
            state->yawDeg += kRoute1BackdropYawStepDeg;
            changed = true;
            break;
        case InputEvent::Key::R:
            *state = session_world_backdrop::defaultRoute1BackdropTuningState();
            state->enabled = true;
            changed = true;
            break;
        default:
            return false;
    }

    if (changed) {
        session_render_scratch::invalidateProjectedBackdrop(
            session_render_scratch::threadScratch());
        logRoute1BackdropTuningState(log, *state);
    }
    return true;
}

} // namespace

void handleEvent(const InputEvent& event, const Context& context) {
    if (!context.pauseState) return;

    session_loop_runtime::handleEvent(
        event,
        *context.pauseState,
        {
            .log = context.log,
            .renderWorldForInput = context.renderWorldForInput,
            .usesBackendGameUiPath = context.usesBackendGameUiPath,
            .onResize =
                [&](int drawableW, int drawableH) {
                    if (context.viewport) {
                        context.viewport->set(drawableW, drawableH);
                    }
                    if (context.unitSystem) {
                        context.unitSystem->setScreenSize(
                            static_cast<unsigned int>(std::max(1, drawableW)),
                            static_cast<unsigned int>(std::max(1, drawableH)));
                    }
                },
            .saveDebugSnapshot = context.saveDebugSnapshot,
            .toggleBackdropTiles =
                [&]() {
                    if (!context.engineServices || !context.log) return;
                    context.engineServices->sessionBackdropTilesEnabled =
                        !context.engineServices->sessionBackdropTilesEnabled;
                    session_render_scratch::invalidateProjectedBackdrop(
                        session_render_scratch::threadScratch());
                    game::log::info(
                        context.log,
                        context.engineServices->sessionBackdropTilesEnabled
                            ? "[Backdrop] SessionWorldBackdrop tiles: On"
                            : "[Backdrop] SessionWorldBackdrop tiles: Off (plain black board/bench)");
                },
            .toggleTerminalLogMode =
                [&]() {
                    if (!context.engineServices || !context.log) return;
                    context.engineServices->terminalLogMode =
                        perf_logging::nextTerminalLogMode(
                            context.engineServices->terminalLogMode);
                    game::log::info(
                        context.log,
                        std::string("[Debug] Terminal log mode: ") +
                            perf_logging::terminalLogModeName(
                                context.engineServices->terminalLogMode));
                },
            .toggleRoute1BackdropTuning =
                [&]() {
                    if (!context.route1BackdropTuning || !context.log) return;
                    context.route1BackdropTuning->enabled =
                        !context.route1BackdropTuning->enabled;
                    game::log::infoTerminalOnly(
                        context.log,
                        context.route1BackdropTuning->enabled
                            ? "[Backdrop][Route1Tune] ON"
                            : "[Backdrop][Route1Tune] OFF");
                    logRoute1BackdropTuningControls(context.log);
                    logRoute1BackdropTuningState(
                        context.log,
                        *context.route1BackdropTuning);
                },
            .loadDebugSnapshot = context.loadDebugSnapshot,
            .openMainMenu =
                [&]() {
                    if (!context.stateManager || !context.gameWorld || !context.services) return;
                    context.stateManager->pushState(std::make_unique<ScriptedState>(
                        context.stateManager,
                        context.gameWorld,
                        *context.services,
                        engine::paths::data("scripts/states/main_menu.lua")));
                },
            .clearSelection =
                [&]() {
                    if (!context.inventoryDependencies) return false;
                    return session_backend_inventory_ui::clearSelection(
                        context.inventoryDependencies());
                },
            .handleInventoryInput =
                [&](const InputEvent& inputEvent) {
                    if (!context.backendInventoryPanel || !context.inventoryDependencies) {
                        return false;
                    }
                    return session_backend_inventory_ui::handleInput(
                        *context.backendInventoryPanel,
                        inputEvent,
                        context.backendInventoryVisibleCount,
                        context.inventoryDependencies());
                },
            .handleRoute1BackdropTuningInput =
                [&](const InputEvent& inputEvent) {
                    return handleRoute1BackdropTuningInput(
                        inputEvent,
                        context.log,
                        context.route1BackdropTuning);
                },
            .handleCameraInput =
                [&](const InputEvent& inputEvent) {
                    if (context.cameraSystem) context.cameraSystem->handleInput(inputEvent);
                },
            .handleUnitInput =
                [&](const InputEvent& inputEvent) {
                    if (context.unitSystem) context.unitSystem->handleInput(inputEvent);
                },
            .handleStateInput =
                [&](const InputEvent& inputEvent) {
                    if (context.stateManager) context.stateManager->handleInput(inputEvent);
                },
        });
}

void fixedUpdate(float dt, const FixedUpdateContext& context) {
    if (!context.pauseState) return;

    session_loop_runtime::fixedUpdate(
        dt,
        *context.pauseState,
        {
            .usesBackendGameRenderPath = context.usesBackendGameRenderPath,
            .advanceTime = context.advanceTime,
            .hydrateBackend = context.hydrateBackend,
            .addBackendHydrateMs =
                [&](float ms) {
                    if (context.engineServices) {
                        context.engineServices->frameFixedBreakdown.backendHydrateMs += ms;
                    }
                },
            .tickUpdateGraph = context.tickUpdateGraph,
        });
}

} // namespace game::runtime::session_loop_bridge
