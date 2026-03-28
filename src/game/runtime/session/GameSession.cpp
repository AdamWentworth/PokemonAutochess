#include "game/runtime/session/GameSession.h"

// Heavy includes live here (not in headers).
#include <iostream>
#include <string>
#include <utility>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <random>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/core/GameContext.h"
#include "engine/core/EngineServices.h"
#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "engine/core/Services.h"
#include "engine/core/TimeSources.h"
#include "engine/core/Environment.h"
#include "engine/core/ecs/Entity.h"
#include "engine/input/InputEvent.h"

#include "engine/render/Camera3D.h"
#include "engine/render/IRenderBackend.h"
#include "engine/render/Model.h"

#include "engine/core/ecs/Scheduler.h"
#include "engine/core/ecs/World.h"
#include "engine/utils/ResourceManager.h"

#include "game/GameWorld.h"
#include "game/GameStateManager.h"
#include "game/runtime/routes/RenderFlowDecisions.h"
#include "game/runtime/routes/StartupRenderRoutePolicy.h"
#include "game/runtime/ui/DebugText.h"
#include "game/runtime/routes/GameServiceRenderRoutes.h"
#include "game/runtime/ui/InventoryPanel.h"
#include "game/runtime/ui/InputSlots.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/startup/RuntimeRenderModelPrewarm.h"
#include "game/runtime/startup/RuntimeGrowlVfxPrewarm.h"
#include "game/runtime/startup/RuntimeParticleVfxPrewarm.h"
#include "game/runtime/startup/RuntimeUiCardPrewarm.h"
#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"
#include "game/runtime/startup/RuntimeWorldLayerPrewarm.h"
#include "game/runtime/session/SessionBackendUnitHydration.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/GameServices.h"
#include "game/GameConfig.h"
#include "game/runtime/session/GameUpdateGraph.h"
#include "game/runtime/session/SessionCoreBootstrapRuntime.h"
#include "game/runtime/session/SessionBackendInventoryUi.h"
#include "game/runtime/session/SessionBackendRenderHelpers.h"
#include "game/runtime/session/SessionDebugSnapshot.h"
#include "game/runtime/session/SessionLoopRuntime.h"
#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/session/SessionRenderConfig.h"
#include "game/runtime/session/SessionSnapshotRuntime.h"
#include "game/runtime/session/SessionStartupRuntime.h"
#include "game/runtime/session/SessionTailFirePrewarm.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "game/runtime/session/SessionWorldRenderRuntime.h"
#include "game/ui/UIViewport.h"
#include "game/ui/ShopLayout.h"

#include "game/config/GameDataDb.h"
#include "game/config/AnimSetLoader.h"
#include "game/assets/DevAssetStore.h"
#include "game/assets/PackedAssetStore.h"
#include "game/PhaseState.h"
#include "game/state/CombatState.h"
#include "game/state/PlacementState.h"

#include "game/systems/CameraSystem.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/MovementSystem.h"
#include "game/systems/CombatSystem.h"
#include "game/systems/ShopSystem.h"
#include "game/systems/LegacySystemAdapters.h"

#include "game/state/scripted/ScriptedState.h"
#include "game/logging/LogBus.h"
#include "game/logging/LoggerUtil.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/world/MoveImpactRouting.h"

namespace {
constexpr int kWorldLayerPrewarmFrames = 2;

std::string debugStateSnapshotPath() {
    return game::runtime::session_debug_snapshot::snapshotPath();
}
} // namespace

namespace game {

struct GameSession::Impl {
    // Pointers (engine-owned)
    Camera3D* camera = nullptr;
    IRenderBackend* renderer = nullptr;
    EngineServices* engineServices = nullptr;

    // Injected db (owned; loader instances).
    GameDataDb dataDb;

    // Game-owned logger instance (no file-scope globals).
    LogBus::Logger log;
    ScriptEventBus scriptEvents;
    std::unique_ptr<engine::IAssetStore> assetStore;
    engine::XorShift32 rng;
    engine::ManualTimeSource timeSource;

    // Owned config (loaded once per session).
    GameConfigData config;

    // v1: thread config/db/log into states without singletons.
    std::unique_ptr<GameServices> services;
    ui::UIViewport viewport;

    // ECS runtime core services + world.
    engine::CoreServices coreServices;
    engine::ecs::World ecsWorld;
    engine::ecs::Entity roundPhaseEntity{};

    // Owned state
    std::unique_ptr<GameStateManager> stateManager;
    std::unique_ptr<GameWorld>        gameWorld;
    engine::ecs::Scheduler scheduler;
    GameUpdateGraph updateGraph;

    runtime::render::RenderRoutes startupRoutes{};
    bool allowBackendMenuBackdrop = false;
    bool showPerfOverlay = false;
    int worldLayerPrewarmFramesRemaining = 0;
    std::function<void(const std::string&)> setTitleCallback;
    game::runtime::session_loop_runtime::PauseState pauseState;

    static constexpr std::size_t kBackendInventoryVisibleCount = 6;
    runtime::ui_inventory_panel::PanelState backendInventoryPanel;
    struct BackendMeshCacheEntry {
        bool attemptedLoad = false;
        bool reportedFailure = false;
        runtime::render_model::MeshData mesh;
        std::string error;
    };
    std::unordered_map<std::string, BackendMeshCacheEntry> backendMeshByModelPath;
    using BackendAnimRoleEntry = game::runtime::session_backend_unit_hydration::BackendAnimRoleEntry;
    using BackendAnimRoleCache = game::runtime::session_backend_unit_hydration::BackendAnimRoleCache;
    BackendAnimRoleCache backendAnimByModelPath;
    using BackendTextureCacheEntry = game::runtime::SharedBackendTextureCacheEntry;
    std::unordered_map<std::string, BackendTextureCacheEntry> backendTextureByPath;

    std::shared_ptr<CameraSystem>           cameraSystem;
    std::shared_ptr<UnitInteractionSystem>  unitSystem;
    ShopSystem*                             shopSystem = nullptr;
    RoundSystem*                            roundSystem = nullptr;


    Impl(GameContext& ctx, GameDataDb db)
        : dataDb(std::move(db))
        , ecsWorld(&coreServices) {
        init(ctx);
    }

    bool hasActiveRenderBackend() const {
        if (services) return services->renderEnabled;
        return startupRoutes.hasRenderer;
    }

    bool usesBackendGameRenderPath() const {
        if (services) return services->usesBackendGameRenderPath();
        return startupRoutes.usesBackendRenderPath();
    }

    bool usesBackendGameUiPath() const {
        if (services) return services->usesBackendGameUiPath();
        return startupRoutes.usesBackendUiPath();
    }

    runtime::render::RenderRoutes activeRenderRoutes() const {
        if (services) {
            return runtime::render::routesFromServices(*services);
        }
        return startupRoutes;
    }

    runtime::render::FrameRenderFlow currentFrameFlow(bool renderWorldRequested) const {
        return runtime::render::decideFrameRenderFlow(
            activeRenderRoutes(),
            renderWorldRequested,
            allowBackendMenuBackdrop);
    }

    runtime::render_model::MeshData* ensureBackendMeshLoaded(const std::string& modelPath) {
        auto& cacheEntry = backendMeshByModelPath[modelPath];
        if (!cacheEntry.attemptedLoad) {
            cacheEntry.attemptedLoad = true;
            std::string err;
            if (!runtime::render_model::loadMeshFromCache(modelPath, cacheEntry.mesh, &err)) {
                cacheEntry.error = std::move(err);
                cacheEntry.mesh = {};
            }
        }

        if (!cacheEntry.error.empty()) {
            if (!cacheEntry.reportedFailure) {
                std::cout << "[Render][ModelCache] Unable to render model '" << modelPath
                          << "' (" << cacheEntry.error << ")\n";
                cacheEntry.reportedFailure = true;
            }
            return nullptr;
        }
        if (cacheEntry.mesh.vertices.empty() || cacheEntry.mesh.indices.empty()) {
            return nullptr;
        }
        return &cacheEntry.mesh;
    }

    BackendTextureCacheEntry* ensureBackendTextureLoaded(const std::string& texturePath,
                                                         bool flipVertical = false) {
        return game::runtime::session_texture_cache::ensureTextureLoaded(
            backendTextureByPath,
            texturePath,
            flipVertical);
    }

    void hydrateBackendUnitAnimationAndScale() {
        if (!usesBackendGameRenderPath() || !gameWorld) return;
        game::runtime::session_backend_unit_hydration::hydrateBackendUnits(
            gameWorld->getPokemons(),
            gameWorld->getBenchPokemons(),
            dataDb,
            backendAnimByModelPath,
            [&](const std::string& modelPath) {
                return ensureBackendMeshLoaded(modelPath);
            });
    }

    void init(GameContext& ctx) {
        camera = ctx.camera;
        renderer = ctx.renderer;
        engineServices = ctx.services;
        setTitleCallback = ctx.setTitle;
        const bool hasBackend = (ctx.renderer != nullptr) && (ctx.camera != nullptr);
        startupRoutes = runtime::render::selectStartupRenderRoutes(hasBackend);
        if (engine::env::get("PAC_BACKEND_MENU_BACKDROP").has_value()) {
            allowBackendMenuBackdrop = engine::env::flagEnabled("PAC_BACKEND_MENU_BACKDROP");
        }
        if (engine::env::get("PAC_SHOW_PERF_OVERLAY").has_value()) {
            showPerfOverlay = engine::env::flagEnabled("PAC_SHOW_PERF_OVERLAY");
        }
        viewport.set(ctx.drawableW, ctx.drawableH);

        game::runtime::session_core_bootstrap_runtime::run(
            {
                .ctx = &ctx,
                .camera = camera,
                .renderer = renderer,
                .engineServices = engineServices,
                .startupRoutes = &startupRoutes,
                .dataDb = &dataDb,
                .log = &log,
                .scriptEvents = &scriptEvents,
                .assetStore = &assetStore,
                .rng = &rng,
                .timeSource = &timeSource,
                .config = &config,
                .services = &services,
                .viewport = &viewport,
                .coreServices = &coreServices,
                .ecsWorld = &ecsWorld,
                .roundPhaseEntity = &roundPhaseEntity,
                .stateManager = &stateManager,
                .gameWorld = &gameWorld,
                .scheduler = &scheduler,
                .updateGraph = &updateGraph,
                .cameraSystem = &cameraSystem,
                .unitSystem = &unitSystem,
                .shopSystem = &shopSystem,
                .roundSystem = &roundSystem,
            });

        game::runtime::session_startup_runtime::run(
            {
                .ctx = &ctx,
                .renderer = renderer,
                .engineServices = engineServices,
                .dataDb = &dataDb,
                .config = &config,
                .services = services.get(),
                .gameWorld = gameWorld.get(),
                .stateManager = stateManager.get(),
                .log = &log,
                .worldLayerPrewarmFramesRemaining = &worldLayerPrewarmFramesRemaining,
                .worldLayerPrewarmFrameCount = kWorldLayerPrewarmFrames,
                .snapshotPath = debugStateSnapshotPath(),
                .autoLoadSnapshotOnStartup =
                    game::runtime::session_debug_snapshot::autoLoadSnapshotEnabled(),
                .usesBackendGameRenderPath = [&]() { return usesBackendGameRenderPath(); },
                .loadModel =
                    [&](const std::string& modelPath) {
                        auto& cacheEntry = backendMeshByModelPath[modelPath];
                        if (cacheEntry.attemptedLoad) {
                            return game::runtime::render_model_prewarm::ModelLoadResult{
                                false,
                                cacheEntry.error.empty() ? &cacheEntry.mesh : nullptr,
                                cacheEntry.error,
                            };
                        }

                        cacheEntry.attemptedLoad = true;
                        std::string err;
                        if (!runtime::render_model::loadMeshFromCache(
                                modelPath, cacheEntry.mesh, &err)) {
                            cacheEntry.error = std::move(err);
                            cacheEntry.mesh = {};
                            return game::runtime::render_model_prewarm::ModelLoadResult{
                                true,
                                nullptr,
                                cacheEntry.error,
                            };
                        }

                        return game::runtime::render_model_prewarm::ModelLoadResult{
                            true,
                            &cacheEntry.mesh,
                            {},
                        };
                    },
                .prewarmAnimRoles =
                    [&](const std::string& modelPath,
                        const runtime::render_model::MeshData& mesh) {
                        auto it = backendAnimByModelPath.find(modelPath);
                        const bool alreadyResolved =
                            (it != backendAnimByModelPath.end()) && it->second.attemptedResolve;
                        BackendAnimRoleEntry& roles =
                            game::runtime::session_backend_unit_hydration::ensureBackendAnimRoles(
                                modelPath,
                                &mesh,
                                backendAnimByModelPath);
                        return !alreadyResolved && roles.attemptedResolve;
                    },
                .prewarmTextures =
                    [&](const std::string&, const runtime::render_model::MeshData& mesh) {
                        return game::runtime::session_backend_render_helpers::prewarmBackendWorldTexturesForMesh(renderer, &mesh);
                    },
                .prewarmGeometry =
                    [&](const runtime::render_model::MeshData& mesh) {
                        return game::runtime::shared_projected_unit_backend_mesh::
                            prewarmProjectedUnitBackendMeshGeometryCache(*renderer, mesh);
                    },
                .prewarmTailFire =
                    [&]() {
                        return game::runtime::session_tail_fire_prewarm::prewarm(
                            {
                                .renderer = renderer,
                                .backendTextureByPath = &backendTextureByPath,
                                .ensureBackendTextureLoaded =
                                    [&](const std::string& texturePath, bool flipVertical) {
                                        return ensureBackendTextureLoaded(texturePath, flipVertical);
                                    },
                            });
                    },
                .prewarmGrowlVfx =
                    [&]() {
                        return game::runtime::growl_vfx_prewarm::prewarm(
                            {
                                .renderer = renderer,
                                .backendTextureByPath = &backendTextureByPath,
                                .ensureBackendMeshLoaded =
                                    [&](const std::string& modelPath) {
                                        return ensureBackendMeshLoaded(modelPath);
                                    },
                                .ensureBackendTextureLoaded =
                                    [&](const std::string& texturePath, bool flipVertical) {
                                        return ensureBackendTextureLoaded(texturePath, flipVertical);
                                    },
                            });
                    },
                .prewarmParticleVfx =
                    [&]() {
                        return game::runtime::particle_vfx_prewarm::prewarm(
                            {
                                .renderer = renderer,
                                .ensureBackendTextureLoaded =
                                    [&](const std::string& texturePath, bool flipVertical) {
                                        return ensureBackendTextureLoaded(texturePath, flipVertical);
                                    },
                            });
                    },
                .renderWorldLayer =
                    [&](int drawableW, int drawableH) {
                        renderWorldLayer(drawableW, drawableH, /*renderWorld=*/true);
                    },
            });

        maybeAutoLoadDebugStateSnapshot();
    }

    game::runtime::session_backend_inventory_ui::Dependencies backendInventoryUiDependencies() {
        return game::runtime::session_backend_inventory_ui::Dependencies{
            .getSelectedItem =
                [&]() -> std::string {
                    return gameWorld ? gameWorld->getSelectedItem() : std::string{};
                },
            .setSelectedItem =
                [&](const std::string& itemId) {
                    if (gameWorld) {
                        gameWorld->setSelectedItem(itemId);
                    }
                },
            .listItems =
                [&]() -> std::vector<std::pair<std::string, int>> {
                    return gameWorld ? gameWorld->listItems()
                                     : std::vector<std::pair<std::string, int>>{};
                },
            .getInventoryRevision =
                [&]() -> std::uint64_t {
                    return gameWorld ? gameWorld->getInventoryUiRevision() : 0u;
                },
            .logInfo = [&](const std::string& message) { log.catchInfo(message); },
        };
    }

    void saveDebugStateSnapshot() {
        game::runtime::session_snapshot_runtime::saveSnapshot(
            debugStateSnapshotPath(),
            {
                .stateManager = stateManager.get(),
                .gameWorld = gameWorld.get(),
                .services = services.get(),
                .log = &log,
            });
    }

    void loadDebugStateSnapshot() {
        game::runtime::session_snapshot_runtime::loadSnapshot(
            debugStateSnapshotPath(),
            {
                .stateManager = stateManager.get(),
                .gameWorld = gameWorld.get(),
                .services = services.get(),
                .roundSystem = roundSystem,
                .log = &log,
                .refreshInventoryPanel =
                    [&]() {
                        game::runtime::session_backend_inventory_ui::refreshPanel(
                            backendInventoryPanel,
                            kBackendInventoryVisibleCount,
                            backendInventoryUiDependencies());
                    },
                .resetRenderCaches =
                    [&]() {
                        game::runtime::session_render_scratch::resetSceneCaches(
                            game::runtime::session_render_scratch::threadScratch());
                    },
                .shouldPrewarmIndexedLayer =
                    [&]() {
                        return game::runtime::session_render_config::snapshotPrewarmRestoreRenderEnabled() &&
                            renderer &&
                            usesBackendGameRenderPath() &&
                            renderer->supportsWorldIndexedMeshes() &&
                            viewport.width > 0 &&
                            viewport.height > 0;
                    },
                .prewarmIndexedLayer =
                    [&]() -> std::size_t {
                        bool renderWorld = true;
                        if (stateManager) {
                            if (auto* state = stateManager->getCurrentState()) {
                                renderWorld = state->shouldRenderWorld();
                            }
                        }
                        return prewarmWorldIndexedLayer(viewport.width, viewport.height, renderWorld);
                    },
            });
    }

    void maybeAutoLoadDebugStateSnapshot() {
        if (!game::runtime::session_debug_snapshot::autoLoadSnapshotEnabled()) {
            return;
        }

        const std::string path = debugStateSnapshotPath();
        if (!std::filesystem::exists(path)) {
            const std::string message =
                std::string("[StateSnapshot] Auto-load requested but snapshot file was not found: ")
                + path;
            game::log::warn(&log, message);
            game::log::infoTerminalOnly(&log, message);
            return;
        }

        loadDebugStateSnapshot();
    }

    void handleEvent(const InputEvent& event) {
        game::runtime::session_loop_runtime::handleEvent(
            event,
            pauseState,
            {
                .log = &log,
                .renderWorldForInput =
                    game::runtime::session_loop_runtime::renderWorldForInput(stateManager.get()),
                .usesBackendGameUiPath = usesBackendGameUiPath(),
                .onResize =
                    [&](int drawableW, int drawableH) {
                        viewport.set(drawableW, drawableH);
                        if (unitSystem) {
                            unitSystem->setScreenSize(
                                static_cast<unsigned int>(std::max(1, drawableW)),
                                static_cast<unsigned int>(std::max(1, drawableH)));
                        }
                    },
                .saveDebugSnapshot = [&]() { saveDebugStateSnapshot(); },
                .toggleBackdropTiles =
                    [&]() {
                        if (!engineServices) return;
                        engineServices->sessionBackdropTilesEnabled =
                            !engineServices->sessionBackdropTilesEnabled;
                        game::runtime::session_render_scratch::invalidateProjectedBackdrop(
                            game::runtime::session_render_scratch::threadScratch());
                        game::log::info(
                            &log,
                            engineServices->sessionBackdropTilesEnabled
                                ? "[Backdrop] SessionWorldBackdrop tiles: On"
                                : "[Backdrop] SessionWorldBackdrop tiles: Off (plain black board/bench)");
                    },
                .toggleTerminalLogMode =
                    [&]() {
                        if (!engineServices) return;
                        engineServices->terminalLogMode =
                            (engineServices->terminalLogMode == EngineTerminalLogMode::Performance)
                                ? EngineTerminalLogMode::GrowlVfx
                                : EngineTerminalLogMode::Performance;
                        game::log::info(
                            &log,
                            engineServices->terminalLogMode == EngineTerminalLogMode::GrowlVfx
                                ? "[Debug] Terminal log mode: Growl VFX"
                                : "[Debug] Terminal log mode: Performance");
                    },
                .loadDebugSnapshot = [&]() { loadDebugStateSnapshot(); },
                .openMainMenu =
                    [&]() {
                        if (!stateManager) return;
                        stateManager->pushState(std::make_unique<ScriptedState>(
                            stateManager.get(),
                            gameWorld.get(),
                            *services,
                            engine::paths::data("scripts/states/main_menu.lua")
                        ));
                    },
                .clearSelection =
                    [&]() {
                        return game::runtime::session_backend_inventory_ui::clearSelection(
                            backendInventoryUiDependencies());
                    },
                .handleInventoryInput =
                    [&](const InputEvent& inputEvent) {
                        return game::runtime::session_backend_inventory_ui::handleInput(
                            backendInventoryPanel,
                            inputEvent,
                            kBackendInventoryVisibleCount,
                            backendInventoryUiDependencies());
                    },
                .handleCameraInput =
                    [&](const InputEvent& inputEvent) {
                        if (cameraSystem) cameraSystem->handleInput(inputEvent);
                    },
                .handleUnitInput =
                    [&](const InputEvent& inputEvent) {
                        if (unitSystem) unitSystem->handleInput(inputEvent);
                    },
                .handleStateInput =
                    [&](const InputEvent& inputEvent) {
                        if (stateManager) stateManager->handleInput(inputEvent);
                    },
            });
    }

    void fixedUpdate(float dt) {
        game::runtime::session_loop_runtime::fixedUpdate(
            dt,
            pauseState,
            {
                .usesBackendGameRenderPath = usesBackendGameRenderPath(),
                .advanceTime = [&](float deltaTime) { timeSource.advance(deltaTime); },
                .hydrateBackend = [&]() { hydrateBackendUnitAnimationAndScale(); },
                .addBackendHydrateMs =
                    [&](float ms) {
                        if (engineServices) {
                            engineServices->frameFixedBreakdown.backendHydrateMs += ms;
                        }
                    },
                .tickUpdateGraph = [&](float deltaTime) { updateGraph.tick(deltaTime); },
            });
    }

    void renderWorldLayer(int drawableW, int drawableH, bool renderWorld) {
        const runtime::render::RenderRoutes routes = activeRenderRoutes();
        if (routes.usesBackendRenderPath()) {
            (void)game::runtime::session_world_render_runtime::render(
                {
                    .renderer = renderer,
                    .engineServices = engineServices,
                    .services = services.get(),
                    .gameWorld = gameWorld.get(),
                    .camera = camera,
                    .ecsWorld = &ecsWorld,
                    .roundPhaseEntity = roundPhaseEntity,
                    .log = &log,
                    .backendInventoryPanel = &backendInventoryPanel,
                    .refreshBackendInventoryFromWorld =
                        [&]() {
                            game::runtime::session_backend_inventory_ui::refreshPanel(
                                backendInventoryPanel,
                                kBackendInventoryVisibleCount,
                                backendInventoryUiDependencies());
                        },
                    .config = &config,
                    .dataDb = &dataDb,
                    .backendTextureByPath = &backendTextureByPath,
                    .routes = routes,
                    .showPerfOverlay = showPerfOverlay,
                    .renderWorld = renderWorld,
                    .enableBackdropTiles =
                        engineServices ? engineServices->sessionBackdropTilesEnabled : true,
                    .allowBackendMenuBackdrop = allowBackendMenuBackdrop,
                    .drawableW = drawableW,
                    .drawableH = drawableH,
                    .simNowSec = timeSource.nowSeconds(),
                    .stateScriptPath = currentStateScriptPath(),
                    .ensureBackendMeshLoaded =
                        [&](const std::string& modelPath) {
                            return ensureBackendMeshLoaded(modelPath);
                        },
                    .ensureBackendTextureLoaded =
                        [&](const std::string& texturePath, bool flipVertical) {
                            return ensureBackendTextureLoaded(texturePath, flipVertical);
                        },
                });
        }
    }

    std::size_t prewarmWorldIndexedLayer(int drawableW, int drawableH, bool renderWorld) {
        const runtime::render::RenderRoutes routes = activeRenderRoutes();
        if (!routes.usesBackendRenderPath()) return 0u;
        return game::runtime::session_world_render_runtime::render(
            {
                .renderer = renderer,
                .engineServices = engineServices,
                .services = services.get(),
                .gameWorld = gameWorld.get(),
                .camera = camera,
                .ecsWorld = &ecsWorld,
                .roundPhaseEntity = roundPhaseEntity,
                .log = &log,
                .backendInventoryPanel = &backendInventoryPanel,
                .refreshBackendInventoryFromWorld =
                    [&]() {
                        game::runtime::session_backend_inventory_ui::refreshPanel(
                            backendInventoryPanel,
                            kBackendInventoryVisibleCount,
                            backendInventoryUiDependencies());
                    },
                .config = &config,
                .dataDb = &dataDb,
                .backendTextureByPath = &backendTextureByPath,
                .routes = routes,
                .showPerfOverlay = showPerfOverlay,
                .renderWorld = renderWorld,
                .enableBackdropTiles =
                    engineServices ? engineServices->sessionBackdropTilesEnabled : true,
                .allowBackendMenuBackdrop = allowBackendMenuBackdrop,
                .prewarmWorldIndexedOnly = true,
                .drawableW = drawableW,
                .drawableH = drawableH,
                .simNowSec = timeSource.nowSeconds(),
                .stateScriptPath = currentStateScriptPath(),
                .ensureBackendMeshLoaded =
                    [&](const std::string& modelPath) {
                        return ensureBackendMeshLoaded(modelPath);
                    },
                .ensureBackendTextureLoaded =
                    [&](const std::string& texturePath, bool flipVertical) {
                        return ensureBackendTextureLoaded(texturePath, flipVertical);
                    },
            });
    }

    std::string currentStateScriptPath() const {
        if (!stateManager) return {};
        GameState* current = stateManager->getCurrentState();
        if (!current) return {};
        if (const auto* combat = dynamic_cast<const CombatState*>(current)) {
            return combat->debugScriptPath();
        }
        if (dynamic_cast<const PlacementState*>(current) != nullptr) {
            // Placement is the first visible planning state and currently
            // always previews Route 1 before the player enters combat.
            return "scripts/states/route1.lua";
        }
        if (const auto* scripted = dynamic_cast<const ScriptedState*>(current)) {
            return scripted->debugScriptPath();
        }
        return {};
    }

    void renderStateLayer() {
        if (stateManager) {
            stateManager->render();
        }
    }

    void renderFrameFromFlow(const runtime::render::FrameRenderFlow& flow,
                             int drawableW,
                             int drawableH,
                             bool renderWorld) {
        if (flow.renderWorldLayer) {
            renderWorldLayer(drawableW, drawableH, renderWorld);
        }
        if (flow.renderStateLayer) {
            renderStateLayer();
        }
    }

    void render(int drawableW, int drawableH) {
        viewport.set(drawableW, drawableH);
        if (unitSystem) {
            unitSystem->setScreenSize(
                static_cast<unsigned int>(std::max(1, drawableW)),
                static_cast<unsigned int>(std::max(1, drawableH)));
        }

        bool renderWorld = true;
        if (stateManager) {
            if (auto* state = stateManager->getCurrentState()) {
                renderWorld = state->shouldRenderWorld();
            }
        }

        const auto flow = currentFrameFlow(renderWorld);
        game::runtime::world_layer_prewarm::maybeRunDeferredFrame(
            worldLayerPrewarmFramesRemaining,
            kWorldLayerPrewarmFrames,
            flow.renderWorldLayer,
            drawableW,
            drawableH,
            game::runtime::world_layer_prewarm::Callbacks{
                .setTitle = setTitleCallback,
                .renderWorldLayer =
                    [&](int prewarmW, int prewarmH) {
                        renderWorldLayer(prewarmW, prewarmH, /*renderWorld=*/true);
                    },
            },
            std::cout);
        renderFrameFromFlow(flow, drawableW, drawableH, renderWorld);
    }

    void shutdown() {
        std::cout << "[Shutdown] Game.\n";

        log.attach(nullptr);
        log.attachCatchFeed(nullptr);
        log.attachEconomyFeed(nullptr);
        shopSystem = nullptr;
        roundSystem = nullptr;
        unitSystem.reset();
        cameraSystem.reset();

        stateManager.reset();
        gameWorld.reset();

        scheduler.clear();

        std::cout << "[Shutdown] Game done.\n";
    }
};

GameSession::GameSession(GameContext& ctx, GameDataDb db)
    : impl_(std::make_unique<Impl>(ctx, std::move(db))) {}

GameSession::~GameSession() = default;

GameSession::GameSession(GameSession&&) noexcept = default;
GameSession& GameSession::operator=(GameSession&&) noexcept = default;

void GameSession::handleEvent(const InputEvent& event) { impl_->handleEvent(event); }
void GameSession::fixedUpdate(float dt) { impl_->fixedUpdate(dt); }
void GameSession::render(int drawableW, int drawableH) { impl_->render(drawableW, drawableH); }
void GameSession::shutdown() { impl_->shutdown(); }

} // namespace game








