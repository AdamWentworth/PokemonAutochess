#include "game/runtime/session/SessionInitBridge.h"

#include "engine/core/Environment.h"
#include "engine/core/GameContext.h"
#include "game/runtime/routes/StartupRenderRoutePolicy.h"
#include "game/runtime/session/SessionCoreBootstrapRuntime.h"
#include "game/runtime/session/SessionStartupBridge.h"
#include "game/ui/UIViewport.h"

namespace game::runtime::session_init_bridge {

void run(const Context& context) {
    if (!context.ctx || !context.camera || !context.renderer || !context.engineServices ||
        !context.setTitleCallback || !context.startupRoutes ||
        !context.allowBackendMenuBackdrop || !context.showPerfOverlay || !context.viewport) {
        return;
    }

    *context.camera = context.ctx->camera;
    *context.renderer = context.ctx->renderer;
    *context.engineServices = context.ctx->services;
    *context.setTitleCallback = context.ctx->setTitle;

    const bool hasBackend =
        (context.ctx->renderer != nullptr) && (context.ctx->camera != nullptr);
    *context.startupRoutes = render::selectStartupRenderRoutes(hasBackend);
    if (engine::env::get("PAC_BACKEND_MENU_BACKDROP").has_value()) {
        *context.allowBackendMenuBackdrop =
            engine::env::flagEnabled("PAC_BACKEND_MENU_BACKDROP");
    }
    if (engine::env::get("PAC_SHOW_PERF_OVERLAY").has_value()) {
        *context.showPerfOverlay = engine::env::flagEnabled("PAC_SHOW_PERF_OVERLAY");
    }
    context.viewport->set(context.ctx->drawableW, context.ctx->drawableH);

    session_core_bootstrap_runtime::run(
        {
            .ctx = context.ctx,
            .camera = *context.camera,
            .renderer = *context.renderer,
            .engineServices = *context.engineServices,
            .startupRoutes = context.startupRoutes,
            .dataDb = context.dataDb,
            .log = context.log,
            .scriptEvents = context.scriptEvents,
            .assetStore = context.assetStore,
            .rng = context.rng,
            .timeSource = context.timeSource,
            .config = context.config,
            .services = context.services,
            .viewport = context.viewport,
            .coreServices = context.coreServices,
            .ecsWorld = context.ecsWorld,
            .roundPhaseEntity = context.roundPhaseEntity,
            .stateManager = context.stateManager,
            .gameWorld = context.gameWorld,
            .scheduler = context.scheduler,
            .updateGraph = context.updateGraph,
            .cameraSystem = context.cameraSystem,
            .unitSystem = context.unitSystem,
            .shopSystem = context.shopSystem,
            .roundSystem = context.roundSystem,
        });

    session_startup_bridge::run(
        {
            .ctx = context.ctx,
            .renderer = *context.renderer,
            .engineServices = *context.engineServices,
            .dataDb = context.dataDb,
            .config = context.config,
            .services = context.services ? context.services->get() : nullptr,
            .gameWorld = context.gameWorld ? context.gameWorld->get() : nullptr,
            .stateManager = context.stateManager ? context.stateManager->get() : nullptr,
            .log = context.log,
            .consoleLog = context.consoleLog,
            .backendAssets = context.backendAssets,
            .worldLayerPrewarmFramesRemaining = context.worldLayerPrewarmFramesRemaining,
            .worldLayerPrewarmFrameCount = context.worldLayerPrewarmFrameCount,
            .snapshotPath = context.snapshotPath,
            .autoLoadSnapshotOnStartup = context.autoLoadSnapshotOnStartup,
            .usesBackendGameRenderPath = context.usesBackendGameRenderPath,
            .renderWorldLayer =
                [&](int drawableW, int drawableH) {
                    if (context.renderWorldLayer) {
                        context.renderWorldLayer(drawableW, drawableH, true);
                    }
                },
        });

    if (context.maybeAutoLoadSnapshot) {
        context.maybeAutoLoadSnapshot();
    }
}

} // namespace game::runtime::session_init_bridge
