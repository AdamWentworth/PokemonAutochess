#include "game/runtime/session/SessionSnapshotController.h"

#include "engine/render/IRenderBackend.h"
#include "game/GameServices.h"
#include "game/GameStateManager.h"
#include "game/logging/LoggerUtil.h"
#include "game/runtime/session/SessionRenderConfig.h"
#include "game/runtime/session/SessionSnapshotRuntime.h"
#include "game/ui/UIViewport.h"

#include <filesystem>
#include <string>

namespace game::runtime::session_snapshot_controller {

void saveDebugStateSnapshot(const std::string& path, const Context& context) {
    game::runtime::session_snapshot_runtime::saveSnapshot(
        path,
        {
            .stateManager = context.stateManager,
            .gameWorld = context.gameWorld,
            .services = context.services,
            .log = context.log,
        });
}

void loadDebugStateSnapshot(const std::string& path, const Context& context) {
    game::runtime::session_snapshot_runtime::loadSnapshot(
        path,
        {
            .stateManager = context.stateManager,
            .gameWorld = context.gameWorld,
            .services = context.services,
            .roundSystem = context.roundSystem,
            .log = context.log,
            .refreshInventoryPanel = context.refreshInventoryPanel,
            .resetRenderCaches = context.resetRenderCaches,
            .shouldPrewarmIndexedLayer =
                [&]() {
                    return game::runtime::session_render_config::
                               snapshotPrewarmRestoreRenderEnabled() &&
                           context.renderer &&
                           context.usesBackendGameRenderPath &&
                           context.usesBackendGameRenderPath() &&
                           context.renderer->supportsWorldIndexedMeshes() &&
                           context.viewport &&
                           context.viewport->width > 0 &&
                           context.viewport->height > 0;
                },
            .prewarmIndexedLayer =
                [&]() -> std::size_t {
                    int drawableW = 0;
                    int drawableH = 0;
                    if (context.viewport) {
                        drawableW = context.viewport->width;
                        drawableH = context.viewport->height;
                    }

                    bool renderWorld = true;
                    if (context.stateManager) {
                        if (auto* state = context.stateManager->getCurrentState()) {
                            renderWorld = state->shouldRenderWorld();
                        }
                    }

                    if (!context.prewarmWorldIndexedLayer) return 0u;
                    return context.prewarmWorldIndexedLayer(drawableW, drawableH, renderWorld);
                },
        });
}

void maybeAutoLoadDebugStateSnapshot(const std::string& path,
                                     bool autoLoadEnabled,
                                     const Context& context) {
    if (!autoLoadEnabled) {
        return;
    }

    if (!std::filesystem::exists(path)) {
        const std::string message =
            std::string("[StateSnapshot] Auto-load requested but snapshot file was not found: ") +
            path;
        game::log::warn(context.log, message);
        game::log::infoTerminalOnly(context.log, message);
        return;
    }

    loadDebugStateSnapshot(path, context);
}

} // namespace game::runtime::session_snapshot_controller
