#include "game/runtime/session/SessionWorldLayerBridge.h"

#include "game/GameStateManager.h"
#include "game/runtime/session/SessionWorldRenderRuntime.h"
#include "game/state/CombatState.h"
#include "game/state/PlacementState.h"
#include "game/state/scripted/ScriptedState.h"

namespace game::runtime::session_world_layer_bridge {

std::string currentStateScriptPath(GameStateManager* stateManager) {
    if (!stateManager) return {};
    GameState* current = stateManager->getCurrentState();
    if (!current) return {};
    if (const auto* combat = dynamic_cast<const CombatState*>(current)) {
        return combat->debugScriptPath();
    }
    if (dynamic_cast<const PlacementState*>(current) != nullptr) {
        // Placement is the first visible planning state and currently always
        // previews Route 1 before the player enters combat.
        return "scripts/states/route1.lua";
    }
    if (const auto* scripted = dynamic_cast<const ScriptedState*>(current)) {
        return scripted->debugScriptPath();
    }
    return {};
}

std::size_t renderWorldLayer(const Context& context,
                             GameStateManager* stateManager,
                             int drawableW,
                             int drawableH,
                             bool renderWorld,
                             bool prewarmWorldIndexedOnly) {
    return game::runtime::session_world_render_runtime::render(
        {
            .renderer = context.renderer,
            .engineServices = context.engineServices,
            .services = context.services,
            .gameWorld = context.gameWorld,
            .camera = context.camera,
            .ecsWorld = context.ecsWorld,
            .roundPhaseEntity = context.roundPhaseEntity,
            .log = context.log,
            .backendInventoryPanel = context.backendInventoryPanel,
            .refreshBackendInventoryFromWorld = context.refreshBackendInventoryFromWorld,
            .config = context.config,
            .dataDb = context.dataDb,
            .backendTextureByPath = context.backendTextureByPath,
            .routes = context.routes,
            .showPerfOverlay = context.showPerfOverlay,
            .renderWorld = renderWorld,
            .enableBackdropTiles = context.enableBackdropTiles,
            .allowBackendMenuBackdrop = context.allowBackendMenuBackdrop,
            .prewarmWorldIndexedOnly = prewarmWorldIndexedOnly,
            .drawableW = drawableW,
            .drawableH = drawableH,
            .simNowSec = context.simNowSec,
            .stateScriptPath = currentStateScriptPath(stateManager),
            .ensureBackendMeshLoaded = context.ensureBackendMeshLoaded,
            .ensureBackendTextureLoaded = context.ensureBackendTextureLoaded,
        });
}

} // namespace game::runtime::session_world_layer_bridge
