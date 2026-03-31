#include "game/runtime/session/SessionCoordinatorBridge.h"

#include "game/runtime/session/SessionInventoryBridge.h"
#include "game/runtime/session/SessionLifecycleBridge.h"
#include "game/runtime/session/SessionLoopBridge.h"
#include "game/runtime/session/SessionRenderBridge.h"
#include "game/runtime/session/SessionSnapshotController.h"
#include "game/runtime/session/SessionWorldLayerBridge.h"

#include "game/runtime/session/SessionRenderScratch.h"

namespace {

game::runtime::session_inventory_bridge::Context makeInventoryContext(
    const game::runtime::session_coordinator_bridge::Context& context) {
    return game::runtime::session_inventory_bridge::Context{
        .gameWorld = context.gameWorld,
        .log = context.log,
        .panel = context.backendInventoryPanel,
        .visibleCount = context.backendInventoryVisibleCount,
    };
}

game::runtime::session_snapshot_controller::Context makeSnapshotContext(
    const game::runtime::session_coordinator_bridge::Context& context);

game::runtime::session_world_layer_bridge::Context makeWorldLayerContext(
    const game::runtime::session_coordinator_bridge::Context& context) {
    return game::runtime::session_world_layer_bridge::Context{
        .renderer = context.renderer,
        .engineServices = context.engineServices,
        .services = context.services,
        .gameWorld = context.gameWorld,
        .camera = context.camera,
        .ecsWorld = context.ecsWorld,
        .roundPhaseEntity = context.roundPhaseEntity,
        .log = context.log,
        .backendInventoryPanel = context.backendInventoryPanel,
        .refreshBackendInventoryFromWorld =
            [&context]() {
                game::runtime::session_inventory_bridge::refreshPanelFromWorld(
                    makeInventoryContext(context));
            },
        .config = context.config,
        .dataDb = context.dataDb,
        .backendTextureByPath = context.backendTextureByPath,
        .routes = context.routes,
        .showPerfOverlay = context.showPerfOverlay,
        .enableBackdropTiles = context.enableBackdropTiles,
        .allowBackendMenuBackdrop = context.allowBackendMenuBackdrop,
        .simNowSec = context.simNowSec,
        .ensureBackendMeshLoaded = context.ensureBackendMeshLoaded,
        .ensureBackendTextureLoaded = context.ensureBackendTextureLoaded,
    };
}

std::size_t renderWorldLayerImpl(const game::runtime::session_coordinator_bridge::Context& context,
                                 int drawableW,
                                 int drawableH,
                                 bool renderWorld,
                                 bool prewarmWorldIndexedOnly) {
    if (!context.routes.usesBackendRenderPath()) return 0u;
    return game::runtime::session_world_layer_bridge::renderWorldLayer(
        makeWorldLayerContext(context),
        context.stateManager,
        drawableW,
        drawableH,
        renderWorld,
        prewarmWorldIndexedOnly);
}

game::runtime::session_snapshot_controller::Context makeSnapshotContext(
    const game::runtime::session_coordinator_bridge::Context& context) {
    return game::runtime::session_snapshot_controller::Context{
        .stateManager = context.stateManager,
        .gameWorld = context.gameWorld,
        .services = context.services,
        .roundSystem = context.roundSystem,
        .log = context.log,
        .consoleLog = context.consoleLog,
        .renderer = context.renderer,
        .viewport = context.viewport,
        .refreshInventoryPanel =
            [&context]() {
                game::runtime::session_inventory_bridge::refreshPanelFromWorld(
                    makeInventoryContext(context));
            },
        .resetRenderCaches =
            [&context]() {
                if (context.resetRenderCaches) {
                    context.resetRenderCaches();
                    return;
                }
                game::runtime::session_render_scratch::resetSceneCaches(
                    game::runtime::session_render_scratch::threadScratch());
            },
        .usesBackendGameRenderPath = [usesBackend = context.usesBackendGameRenderPath]() {
            return usesBackend;
        },
        .prewarmWorldIndexedLayer =
            [&context](int drawableW, int drawableH, bool renderWorld) {
                return renderWorldLayerImpl(context, drawableW, drawableH, renderWorld, true);
            },
    };
}

game::runtime::session_loop_bridge::Context makeLoopContext(
    const game::runtime::session_coordinator_bridge::Context& context) {
    return game::runtime::session_loop_bridge::Context{
        .log = context.log,
        .pauseState = context.pauseState,
        .engineServices = context.engineServices,
        .viewport = context.viewport,
        .unitSystem = context.unitSystem,
        .cameraSystem = context.cameraSystem,
        .stateManager = context.stateManager,
        .gameWorld = context.gameWorld,
        .services = context.services,
        .backendInventoryPanel = context.backendInventoryPanel,
        .backendInventoryVisibleCount = context.backendInventoryVisibleCount,
        .renderWorldForInput = context.renderWorldForInput,
        .usesBackendGameUiPath = context.usesBackendGameUiPath,
        .inventoryDependencies =
            [&context]() {
                return game::runtime::session_inventory_bridge::makeDependencies(
                    makeInventoryContext(context));
            },
        .saveDebugSnapshot = [&context]() {
            game::runtime::session_coordinator_bridge::saveDebugStateSnapshot(context);
        },
        .loadDebugSnapshot = [&context]() {
            game::runtime::session_coordinator_bridge::loadDebugStateSnapshot(context);
        },
    };
}

game::runtime::session_loop_bridge::FixedUpdateContext makeFixedUpdateContext(
    const game::runtime::session_coordinator_bridge::Context& context) {
    return game::runtime::session_loop_bridge::FixedUpdateContext{
        .pauseState = context.pauseState,
        .engineServices = context.engineServices,
        .usesBackendGameRenderPath = context.usesBackendGameRenderPath,
        .advanceTime = context.advanceTime,
        .hydrateBackend = context.hydrateBackend,
        .tickUpdateGraph = context.tickUpdateGraph,
    };
}

game::runtime::session_render_bridge::Context makeRenderContext(
    const game::runtime::session_coordinator_bridge::Context& context) {
    return game::runtime::session_render_bridge::Context{
        .viewport = context.viewport,
        .worldLayerPrewarmFramesRemaining = context.worldLayerPrewarmFramesRemaining,
        .worldLayerPrewarmFrameCount = context.worldLayerPrewarmFrameCount,
        .consoleLog = context.consoleLog,
        .setUnitScreenSize = context.setUnitScreenSize,
        .resolveRenderWorld = context.resolveRenderWorld,
        .currentFrameFlow = context.currentFrameFlow,
        .setTitle = context.setTitle,
        .renderWorldLayer =
            [&context](int drawableW, int drawableH, bool renderWorld) {
                game::runtime::session_coordinator_bridge::renderWorldLayer(
                    context,
                    drawableW,
                    drawableH,
                    renderWorld);
            },
        .renderStateLayer = context.renderStateLayer,
    };
}

game::runtime::session_lifecycle_bridge::Context makeLifecycleContext(
    const game::runtime::session_coordinator_bridge::Context& context) {
    return game::runtime::session_lifecycle_bridge::Context{
        .consoleLog = context.consoleLog,
        .log = context.log,
        .shopSystem = context.shopSystem,
        .roundSystem = context.roundSystemRef,
        .unitSystem = context.unitSystemRef,
        .cameraSystem = context.cameraSystemRef,
        .stateManager = context.stateManagerRef,
        .gameWorld = context.gameWorldRef,
        .scheduler = context.scheduler,
    };
}

} // namespace

namespace game::runtime::session_coordinator_bridge {

void saveDebugStateSnapshot(const Context& context) {
    game::runtime::session_snapshot_controller::saveDebugStateSnapshot(
        context.snapshotPath,
        makeSnapshotContext(context));
}

void loadDebugStateSnapshot(const Context& context) {
    game::runtime::session_snapshot_controller::loadDebugStateSnapshot(
        context.snapshotPath,
        makeSnapshotContext(context));
}

void maybeAutoLoadDebugStateSnapshot(const Context& context) {
    game::runtime::session_snapshot_controller::maybeAutoLoadDebugStateSnapshot(
        context.snapshotPath,
        context.autoLoadSnapshotOnStartup,
        makeSnapshotContext(context));
}

void handleEvent(const InputEvent& event, const Context& context) {
    game::runtime::session_loop_bridge::handleEvent(event, makeLoopContext(context));
}

void fixedUpdate(float dt, const Context& context) {
    game::runtime::session_loop_bridge::fixedUpdate(dt, makeFixedUpdateContext(context));
}

void renderWorldLayer(const Context& context, int drawableW, int drawableH, bool renderWorld) {
    (void)renderWorldLayerImpl(context, drawableW, drawableH, renderWorld, false);
}

std::size_t prewarmWorldIndexedLayer(const Context& context,
                                     int drawableW,
                                     int drawableH,
                                     bool renderWorld) {
    return renderWorldLayerImpl(context, drawableW, drawableH, renderWorld, true);
}

void render(int drawableW, int drawableH, const Context& context) {
    game::runtime::session_render_bridge::render(
        drawableW,
        drawableH,
        makeRenderContext(context));
}

void shutdown(const Context& context) {
    game::runtime::session_lifecycle_bridge::shutdown(makeLifecycleContext(context));
}

} // namespace game::runtime::session_coordinator_bridge
