#include "game/runtime/session/SessionWorldLayerBridge.h"

#include "game/GameStateManager.h"
#include "game/runtime/session/SessionWorldRenderRuntime.h"
#include "game/state/CombatState.h"
#include "game/state/ArenaTravelState.h"
#include "game/runtime/session/SessionWorldBackdrop.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"
#include "game/state/PlacementState.h"
#include "game/state/scripted/ScriptedState.h"

namespace game::runtime::session_world_layer_bridge {

std::string currentStateScriptPath(GameStateManager* stateManager) {
    if (!stateManager) return {};
    GameState* current = stateManager->getCurrentState();
    if (!current) return {};
    if (const auto* travel = dynamic_cast<const ArenaTravelState*>(current)) return travel->debugScriptPath();
    if (const auto* combat = dynamic_cast<const CombatState*>(current)) {
        return combat->arenaScriptPath();
    }
    if (const auto* placement = dynamic_cast<const PlacementState*>(current)) {
        return placement->debugScriptPath();
    }
    if (const auto* scripted = dynamic_cast<const ScriptedState*>(current)) {
        return scripted->arenaScriptPath().empty() ? scripted->debugScriptPath() : scripted->arenaScriptPath();
    }
    return {};
}

std::size_t renderWorldLayer(const Context& context,
                             GameStateManager* stateManager,
                             int drawableW,
                             int drawableH,
                             bool renderWorld,
                             bool prewarmWorldIndexedOnly) {
    const auto result = game::runtime::session_world_render_runtime::render(
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
            .route1BackdropTuning = context.route1BackdropTuning,
            .ensureBackendMeshLoaded = context.ensureBackendMeshLoaded,
            .ensureBackendTextureLoaded = context.ensureBackendTextureLoaded,
        });
    if (renderWorld && !prewarmWorldIndexedOnly && stateManager) {
        if (auto* travel = dynamic_cast<ArenaTravelState*>(stateManager->getCurrentState())) {
            const auto& scratch = session_render_scratch::threadScratch();
            const auto& variant = route1_scene_variants::fromStateScriptPath(travel->debugScriptPath());
            travel->worldFramePresented(scratch.route1RuntimeEnvironment && scratch.route1RuntimeEnvironment->loaded() &&
                                       scratch.route1RuntimeSceneId == variant.sceneId);
        }
    }
    return result;
}

} // namespace game::runtime::session_world_layer_bridge
