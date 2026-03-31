#include "game/runtime/session/SessionLoopBridge.h"

#include "engine/core/EngineServices.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/GameStateManager.h"
#include "game/GameWorld.h"
#include "game/logging/LoggerUtil.h"
#include "game/runtime/loop/RuntimePerfLogging.h"
#include "game/runtime/session/SessionRenderScratch.h"
#include "game/state/scripted/ScriptedState.h"
#include "game/systems/CameraSystem.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/ui/UIViewport.h"

#include <algorithm>
#include <memory>
#include <string>

namespace game::runtime::session_loop_bridge {

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
