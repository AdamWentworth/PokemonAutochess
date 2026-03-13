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
#include "game/runtime/routes/BackendRenderPolicy.h"
#include "game/runtime/routes/RenderFlowDecisions.h"
#include "game/runtime/routes/StartupRenderRoutePolicy.h"
#include "game/runtime/ui/DebugText.h"
#include "game/runtime/routes/GameServiceRenderRoutes.h"
#include "game/runtime/ui/InventoryOverlay.h"
#include "game/runtime/ui/InventoryPanel.h"
#include "game/runtime/ui/InputSlots.h"
#include "game/runtime/ui/StatusText.h"
#include "game/runtime/ui/UiScale.h"
#include "game/runtime/ui/HudFormatting.h"
#include "game/runtime/render_prep/WorldProjection.h"
#include "game/runtime/render_prep/WorldProxyGeometry.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/render_prep/MaterialShading.h"
#include "game/runtime/render_prep/ProceduralPose.h"
#include "game/runtime/render_prep/UnitVisuals.h"
#include "game/runtime/startup/RuntimeRenderModelPrewarm.h"
#include "game/runtime/startup/RuntimeUiCardPrewarm.h"
#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"
#include "game/runtime/startup/RuntimeWorldLayerPrewarm.h"
#include "game/runtime/session/SessionBackendUnitHydration.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/capture/SharedCapturePresentation.h"
#include "game/runtime/shared/capture/SharedCaptureModelBridge.h"
#include "game/runtime/shared/world/SharedBoardGridBatches.h"
#include "game/runtime/shared/projected/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/shared/ui/SharedBackendDebugViewOverlay.h"
#include "game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/shared/projected/SharedProjectedUnitRenderer.h"
#include "game/runtime/shared/vfx/particles/SharedParticleBillboardBatches.h"
#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"
#include "game/runtime/shared/vfx/particles/SharedParticleVfxBridgeDispatch.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireExactGpuBatches.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireAtlasHelpers.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSnapshotAtlasCache.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlVfxHelpers.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBridge.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBatches.h"
#include "game/runtime/shared/ui/SharedUnitHudBatches.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "game/GameServices.h"
#include "game/GameConfig.h"
#include "game/runtime/session/GameUpdateGraph.h"
#include "game/runtime/session/SessionBackendInventoryUi.h"
#include "game/runtime/session/SessionBackendRenderHelpers.h"
#include "game/runtime/session/SessionDebugSnapshot.h"
#include "game/runtime/session/SessionFrameMetrics.h"
#include "game/runtime/session/SessionLegacyWorldView.h"
#include "game/runtime/session/SessionLoopRuntime.h"
#include "game/runtime/session/SessionProjectedWorldView.h"
#include "game/runtime/session/SessionRenderLayout.h"
#include "game/runtime/session/SessionRenderConfig.h"
#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/session/SessionSnapshotRuntime.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "game/runtime/session/SessionWorldBackdrop.h"
#include "game/ui/UIViewport.h"
#include "game/ui/ShopLayout.h"

#include "game/config/GameDataDb.h"
#include "game/config/AnimSetLoader.h"
#include "game/assets/DevAssetStore.h"
#include "game/assets/PackedAssetStore.h"
#include "game/PhaseState.h"

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

    game::runtime::startup_asset_prewarm::TailFireStats prewarmSharedTailFireAssets() {
        if (!renderer) return {};

        const TailFireVFX::Config& cfg =
            game::runtime::shared_projected_scene::getTailFireFallbackCfg();
        if (!cfg.useFlipbook || cfg.flipbookPath.empty()) return {};

        ParticleSystem::RenderSnapshot snapshot{};
        snapshot.useFlipbook = cfg.useFlipbook;
        snapshot.flipbookPath = cfg.flipbookPath;
        snapshot.flipbookCols = cfg.flipbookCols;
        snapshot.flipbookRows = cfg.flipbookRows;
        snapshot.flipbookFrames = cfg.flipbookFrames;
        snapshot.flipbookFps = cfg.flipbookFps;
        snapshot.useSecondaryFlipbook = cfg.useFlipbook2 && !cfg.flipbook2Path.empty();
        snapshot.flipbookPath2 = cfg.flipbook2Path;
        snapshot.flipbookCols2 = cfg.flipbook2Cols;
        snapshot.flipbookRows2 = cfg.flipbook2Rows;
        snapshot.flipbookFrames2 = cfg.flipbook2Frames;
        snapshot.flipbookFps2 = cfg.flipbook2Fps;

        auto ensureTextureFn =
            [&](const std::string& path, bool flip) -> BackendTextureCacheEntry* {
                return ensureBackendTextureLoaded(path, flip);
            };

        game::runtime::startup_asset_prewarm::TailFireStats warmed{};
        const auto prewarmAtlas = [&](const std::string& key,
                                      const BackendTextureCacheEntry* atlas) {
            if (!atlas || !atlas->valid || atlas->rgba.empty() ||
                atlas->width <= 0 || atlas->height <= 0) {
                return;
            }

            IRenderBackend::WorldTextureData tex{};
            tex.key = key.c_str();
            tex.rgba = atlas->rgba.data();
            tex.width = atlas->width;
            tex.height = atlas->height;
            tex.wrapS = 33071; // GL_CLAMP_TO_EDGE
            tex.wrapT = 33071; // GL_CLAMP_TO_EDGE
            tex.alphaMode = 2u;
            tex.blendMode = 2u;
            renderer->prewarmWorldTextureData(&tex);
            ++warmed.legacyAtlases;
        };

        const auto combined =
            game::runtime::shared_tail_fire_snapshot_billboards::resolveTailFireCombinedAtlas(
                snapshot,
                backendTextureByPath,
                ensureTextureFn);
        if (!combined.cacheKey.empty()) {
            prewarmAtlas(combined.cacheKey, combined.atlas);
        }

        const bool prewarmLegacyPremul =
            game::runtime::session_render_config::backendPrewarmLegacyTailFirePremulEnabled() ||
            !snapshot.useSecondaryFlipbook ||
            !(combined.atlas && combined.atlas->valid);
        if (prewarmLegacyPremul) {
            const std::string primaryPremulKey =
                std::string("__tailfire_premul:") + snapshot.flipbookPath;
            BackendTextureCacheEntry* primaryPremul =
                game::runtime::shared_tail_fire_snapshot_billboards::resolveTailFirePremulAtlas(
                    snapshot.flipbookPath,
                    backendTextureByPath,
                    ensureTextureFn);
            prewarmAtlas(primaryPremulKey, primaryPremul);

            if (snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty()) {
                const std::string secondaryPremulKey =
                    std::string("__tailfire_premul:") + snapshot.flipbookPath2;
                BackendTextureCacheEntry* secondaryPremul =
                    game::runtime::shared_tail_fire_snapshot_billboards::resolveTailFirePremulAtlas(
                        snapshot.flipbookPath2,
                        backendTextureByPath,
                        ensureTextureFn);
                prewarmAtlas(secondaryPremulKey, secondaryPremul);
            }
        }

        const auto& authoredSpecs =
            game::runtime::shared_tail_fire_mesh_playback::authoredFlipbookSpecs();
        if (!authoredSpecs.empty()) {
            const auto& charmanderSpec = authoredSpecs.front();
            if (charmanderSpec.path && charmanderSpec.path[0] != '\0') {
                const auto cpuLoadStart = std::chrono::steady_clock::now();
                BackendTextureCacheEntry* authoredCpuTexture =
                    ensureBackendTextureLoaded(charmanderSpec.path, false);
                const auto cpuLoadEnd = std::chrono::steady_clock::now();
                std::cout << "[TailFire][CPU] authored_mesh_flipbook path="
                          << charmanderSpec.path
                          << " load_ms="
                          << std::chrono::duration<double, std::milli>(cpuLoadEnd - cpuLoadStart).count()
                          << " size="
                          << ((authoredCpuTexture && authoredCpuTexture->valid) ? authoredCpuTexture->width : 0)
                          << "x"
                          << ((authoredCpuTexture && authoredCpuTexture->valid) ? authoredCpuTexture->height : 0)
                          << " result="
                          << ((authoredCpuTexture && authoredCpuTexture->valid) ? "ok" : "failed")
                          << "\n";
                if (authoredCpuTexture && authoredCpuTexture->valid) {
                    ++warmed.meshFlipbookCpu;
                    IRenderBackend::WorldTextureData tex{};
                    tex.key = charmanderSpec.path;
                    tex.cacheKey = charmanderSpec.path;
                    tex.rgba = authoredCpuTexture->rgba.data();
                    tex.width = authoredCpuTexture->width;
                    tex.height = authoredCpuTexture->height;
                    tex.wrapS = 33071; // GL_CLAMP_TO_EDGE
                    tex.wrapT = 33071; // GL_CLAMP_TO_EDGE
                    tex.alphaMode = 1u;
                    tex.blendMode = 0u;
                    renderer->prewarmWorldTextureData(&tex);
                    ++warmed.meshFlipbookGpu;
                }
            }
        }

        return warmed;
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

        const std::string packPath = engine::paths::dataPack();
        if (!packPath.empty()) {
            auto pack = std::make_unique<assets::PackedAssetStore>();
            std::string err;
            if (pack->open(packPath, &err)) {
                assetStore = std::move(pack);
                game::log::info(&log, std::string("[Init] Using packed data bundle: ") + packPath);
            } else {
                game::log::warn(&log, std::string("[Init] Failed to open pack: ") + packPath +
                    (err.empty() ? "" : (" (" + err + ")")));
            }
        }
        if (!assetStore) {
            auto dev = std::make_unique<assets::DevAssetStore>(engine::paths::dataRoot());
            assetStore = std::move(dev);
        }

        {
            std::uint32_t seed = 0;
            bool hasSeed = false;
            if (const auto v = engine::env::get("PAC_RANDOM_SEED")) {
                try {
                    seed = static_cast<std::uint32_t>(std::stoul(*v));
                    hasSeed = true;
                } catch (...) {
                    game::log::warn(&log, std::string("[Init] Invalid PAC_RANDOM_SEED value: ") + *v);
                }
            }
            if (!hasSeed) {
                std::random_device rd;
                seed = (static_cast<std::uint32_t>(rd()) << 16) ^ static_cast<std::uint32_t>(rd());
            }
            rng.reseed(seed);
            game::log::info(&log, std::string("[Init] RNG seed: ") + std::to_string(seed));
        }

        roundPhaseEntity = ecsWorld.create();
        ecsWorld.add<game::CombatActive>(roundPhaseEntity, game::CombatActive{false});

        config = GameConfig::load(&log, assetStore.get());
        services = std::make_unique<GameServices>(config, dataDb, log, scriptEvents, *assetStore, rng, timeSource,
                                                  &ecsWorld, roundPhaseEntity, &viewport, startupRoutes.hasRenderer);
        services->renderer = renderer;
        services->engineServices = ctx.services;
        services->applyVideoMode = ctx.applyVideoMode;
        services->requestQuit = ctx.requestQuit;
        if (ctx.services) {
            services->requestedRendererBackend = ctx.services->requestedRendererBackend;
            services->activeRendererBackend = ctx.services->activeRendererBackend;
            services->rendererBackendFallback = ctx.services->rendererBackendFallback;
            services->gpuVendor = ctx.services->gpuVendor;
            services->gpuRenderer = ctx.services->gpuRenderer;
            services->availableGpuAdapters = ctx.services->availableGpuAdapters;
            services->preferredGpuAdapter = ctx.services->preferredGpuAdapter;
            services->gpuDiscrete = ctx.services->gpuDiscrete;
            services->vsyncEnabled = ctx.services->vsyncEnabled;
            services->requireDiscreteGpu = ctx.services->requireDiscreteGpu;
            services->characterInkingEnabled = ctx.services->characterInkingEnabled;
            services->bootMenuScreen = ctx.services->bootMenuScreen;
        }
        if (ctx.queryVideoMode) {
            services->queryVideoMode = [q = ctx.queryVideoMode]() {
                auto vm = q();
                GameServices::VideoMode out;
                out.width = vm.width;
                out.height = vm.height;
                out.fullscreen = vm.fullscreen;
                return out;
            };
        }
        coreServices.rng = &services->rng;
        coreServices.time = &services->time;

        // World
        gameWorld = std::make_unique<GameWorld>(config);
        gameWorld->setRenderEnabled(hasActiveRenderBackend());
        gameWorld->setLogger(&log);
        gameWorld->setRng(&services->rng);
        if (ctx.services) gameWorld->setResources(ctx.services->resources);
        gameWorld->setData(&dataDb);

        // State stack
        stateManager = std::make_unique<GameStateManager>();

        // Systems
        if (camera) {
            cameraSystem = std::make_shared<CameraSystem>(camera, *services);
            unitSystem   = std::make_shared<UnitInteractionSystem>(camera, gameWorld.get(), ctx.drawableW, ctx.drawableH);
        }
        using Phase = engine::ecs::Scheduler::Phase;

        if (cameraSystem) {
            scheduler.add(
                std::make_unique<game::UpdatableSystemAdapter>(cameraSystem.get(), "camera"),
                Phase::Update);
        }
        if (unitSystem) {
            scheduler.add(
                std::make_unique<game::UpdatableSystemAdapter>(unitSystem.get(), "unit_interaction"),
                Phase::Update);
        }
        auto shopSystemImpl = std::make_unique<ShopSystem>(services->rng);
        shopSystem = shopSystemImpl.get();
        scheduler.add(std::move(shopSystemImpl), Phase::Update);

        auto roundSystemImpl = std::make_unique<RoundSystem>(*services, roundPhaseEntity);
        roundSystem = roundSystemImpl.get();
        ecsWorld.add<game::RoundState>(roundPhaseEntity, game::RoundState{ roundSystemImpl->getCurrentPhase() });
        scheduler.add(std::move(roundSystemImpl), Phase::Update);

        if (auto* stateMgr = stateManager.get()) {
            scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
                [stateMgr, engineServices = engineServices](float dt) {
                    stateMgr->update(dt);
                    if (engineServices) {
                        const auto& timing = stateMgr->lastUpdateTiming();
                        engineServices->frameFixedBreakdown.stateUpdateMs += timing.stateUpdateMs;
                        engineServices->frameFixedBreakdown.stateFlushMs += timing.flushPendingMs;
                    }
                },
                "state_manager"
            ), Phase::PostUpdate);
        }
        if (auto* worldPtr = gameWorld.get()) {
            auto movementSystem = std::make_unique<MovementSystem>(worldPtr, *services, roundPhaseEntity);
            scheduler.add(std::move(movementSystem), Phase::PostUpdate);

            auto combatSystem = std::make_unique<CombatSystem>(worldPtr, *services, roundPhaseEntity);
            scheduler.add(std::move(combatSystem), Phase::PostUpdate);
        }
        if (auto* worldPtr = gameWorld.get()) {
            scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
                [worldPtr](float dt) { worldPtr->update(dt); },
                "world"
            ), Phase::PostUpdate);
        }

        updateGraph.configure({
            &scheduler,
            &ecsWorld,
            roundPhaseEntity,
            shopSystem,
            &log,
            &scriptEvents,
            engineServices
        });

        std::cout << "[Init] Shared gameplay render path: using render model cache loader.\n";
        if (game::runtime::session_render_config::backendPreloadModelCacheEnabled()) {
            std::cout << "[Init] Shared gameplay render path: preloading render model cache...\n";
            const bool prewarmModelTextures =
                usesBackendGameRenderPath() &&
                renderer &&
                renderer->supportsWorldIndexedMeshes() &&
                game::runtime::session_render_config::backendPrewarmModelTexturesEnabled();
            const bool prewarmModelGeometry =
                usesBackendGameRenderPath() &&
                renderer &&
                renderer->supportsWorldIndexedMeshes() &&
                game::runtime::session_render_config::backendModelFullMeshEnabled() &&
                game::runtime::session_render_config::backendModelFastTexturedPathEnabled() &&
                game::runtime::session_render_config::backendPrewarmModelGeometryEnabled();
            std::vector<std::string> modelPathsToPreload;
            modelPathsToPreload.reserve(dataDb.pokemon.all().size());
            std::unordered_set<std::string> seenModelPaths;
            seenModelPaths.reserve(dataDb.pokemon.all().size());
            for (const auto& [name, stats] : dataDb.pokemon.all()) {
                (void)name;
                if (stats.model.empty()) continue;
                const std::string modelPath = "assets/models/" + stats.model;
                if (seenModelPaths.insert(modelPath).second) {
                    modelPathsToPreload.push_back(modelPath);
                }
            }
            // Shared capture uses pokeball.glb as well; preload it by default so first-use
            // capture interactions avoid cache/rebuild cost in active gameplay.
            if (!engine::env::equals("PAC_BACKEND_PRELOAD_CAPTURE_POKEBALL", "0")) {
                const std::string sharedCapturePokeballPath = "assets/models/pokeball.glb";
                if (seenModelPaths.insert(sharedCapturePokeballPath).second) {
                    modelPathsToPreload.push_back(sharedCapturePokeballPath);
                }
            }
            const game::runtime::render_model_prewarm::Options prewarmOptions{
                .verboseModelCacheLog = game::runtime::session_render_config::backendModelVerboseLoggingEnabled(),
                .prewarmAnimRoles = game::runtime::session_render_config::backendPrewarmAnimRolesEnabled(),
                .prewarmModelTextures = prewarmModelTextures,
                .prewarmModelGeometry = prewarmModelGeometry,
                .maxFailureSamples = 8u,
            };
            (void)game::runtime::render_model_prewarm::run(
                modelPathsToPreload,
                prewarmOptions,
                game::runtime::render_model_prewarm::Callbacks{
                    .setTitle = ctx.setTitle,
                    .renderBootLoading = ctx.renderBootLoading,
                    .pumpPreloadEvents = ctx.pumpPreloadEvents,
                    .requestQuit = ctx.requestQuit,
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
                },
                std::cout);
        } else {
            std::cout << "[Init] Shared gameplay render path: render model cache preload disabled.\n";
        }

        // OpenGL shared route renders capture pokeball via the OpenGL Model path (ResourceManager),
        // not the backend mesh cache. Prewarm up front to avoid first-use hitching.
        if (usesBackendGameRenderPath() &&
            renderer &&
            renderer->backendId() &&
            std::string(renderer->backendId()) == "opengl" &&
            engineServices &&
            engineServices->resources) {
            (void)engineServices->resources->getModel("assets/models/pokeball.glb");
        }

        const bool usesBackendPathForStartupPrewarm = usesBackendGameRenderPath() && renderer;
        std::vector<std::string> uiSpritePrewarmPaths;
        if (usesBackendPathForStartupPrewarm && game::runtime::session_render_config::backendUiSpritePrewarmEnabled()) {
            uiSpritePrewarmPaths =
                game::runtime::startup_asset_prewarm::collectUiSpritePrewarmPaths(dataDb);
        }

        (void)game::runtime::startup_asset_prewarm::run(
            game::runtime::startup_asset_prewarm::Options{
                .usesBackendRenderPath = usesBackendPathForStartupPrewarm,
                .uiSpritePrewarmEnabled = game::runtime::session_render_config::backendUiSpritePrewarmEnabled(),
                .drawableW = ctx.drawableW,
                .drawableH = ctx.drawableH,
            },
            uiSpritePrewarmPaths,
            game::runtime::startup_asset_prewarm::Callbacks{
                .setTitle = ctx.setTitle,
                .renderBootLoading = ctx.renderBootLoading,
                .pumpPreloadEvents = ctx.pumpPreloadEvents,
                .requestQuit = ctx.requestQuit,
                .prewarmWorldShading =
                    [&]() {
                        if (renderer) {
                            renderer->prewarmWorldRenderAssets();
                        }
                    },
                .prewarmTailFire =
                    [&]() {
                        return prewarmSharedTailFireAssets();
                    },
                .prewarmSpriteTextures =
                    [&](const std::vector<std::string>& texturePaths) {
                        if (!renderer || texturePaths.empty()) return;
                        std::vector<const char*> rawPaths;
                        rawPaths.reserve(texturePaths.size());
                        for (const std::string& path : texturePaths) {
                            rawPaths.push_back(path.c_str());
                        }
                        renderer->prewarmDebugSpriteTextures(rawPaths.data(), rawPaths.size());
                    },
                .prewarmBackendCardUi =
                    [&](int drawableW,
                        int drawableH,
                        const std::vector<std::string>& texturePaths) {
                        (void)game::runtime::ui_card_prewarm::run(
                            renderer,
                            drawableW,
                            drawableH,
                            texturePaths);
                    },
            },
            std::cout);

        if (usesBackendGameRenderPath() &&
            renderer &&
            game::runtime::session_render_config::backendWorldLayerPrewarmEnabled()) {
            game::runtime::world_layer_prewarm::schedule(
                worldLayerPrewarmFramesRemaining,
                kWorldLayerPrewarmFrames,
                game::runtime::world_layer_prewarm::Callbacks{
                    .setTitle = ctx.setTitle,
                    .renderBootLoading = ctx.renderBootLoading,
                    .pumpPreloadEvents = ctx.pumpPreloadEvents,
                    .requestQuit = ctx.requestQuit,
                    .renderWorldLayer =
                        [&](int drawableW, int drawableH) {
                            renderWorldLayer(drawableW, drawableH, /*renderWorld=*/true);
                        },
                },
                std::cout);
        }

        stateManager->pushState(std::make_unique<ScriptedState>(
            stateManager.get(),
            gameWorld.get(),
            *services,
            engine::paths::data("scripts/states/main_menu.lua")
        ));

        if (worldLayerPrewarmFramesRemaining > 0 &&
            ctx.drawableW > 0 &&
            ctx.drawableH > 0 &&
            usesBackendGameRenderPath() &&
            renderer) {
            game::runtime::world_layer_prewarm::drainStartupFrames(
                worldLayerPrewarmFramesRemaining,
                kWorldLayerPrewarmFrames,
                ctx.drawableW,
                ctx.drawableH,
                game::runtime::world_layer_prewarm::Callbacks{
                    .setTitle = ctx.setTitle,
                    .renderBootLoading = ctx.renderBootLoading,
                    .pumpPreloadEvents = ctx.pumpPreloadEvents,
                    .requestQuit = ctx.requestQuit,
                    .renderWorldLayer =
                        [&](int drawableW, int drawableH) {
                            renderWorldLayer(drawableW, drawableH, /*renderWorld=*/true);
                        },
                },
                std::cout);
        }

        game::runtime::world_layer_prewarm::restoreTitleAfterInit(
            worldLayerPrewarmFramesRemaining,
            game::runtime::world_layer_prewarm::Callbacks{
                .setTitle = ctx.setTitle,
            });
        const std::string snapshotPath = debugStateSnapshotPath();
        if (std::filesystem::exists(snapshotPath)) {
            std::cout << "[StateSnapshot] Snapshot present but not auto-loaded: "
                      << snapshotPath << " (press F9 to restore)\n";
        }
        std::cout << "[Init] Game initialized.\n";

        // Keep startup/perf stdout intact, but stop mirroring gameplay feed spam
        // to the terminal unless explicitly requested.
        if (engine::env::get("PAC_LOG_ECHO_STDOUT").has_value()) {
            log.setEchoToStdout(engine::env::flagEnabled("PAC_LOG_ECHO_STDOUT"));
        } else {
            log.setEchoToStdout(false);
        }
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

    std::size_t renderBackendDebugView(int drawableW,
                                       int drawableH,
                                       bool renderWorld,
                                       bool prewarmWorldIndexedOnly = false) {
        if (!renderer || drawableW <= 0 || drawableH <= 0) return 0u;
        using RenderBuildClock = std::chrono::steady_clock;
        const auto worldComposeStart = RenderBuildClock::now();
        const bool useLegacyGrowlWaveVfx = game::runtime::session_render_config::backendUseLegacyGrowlWaveVfxEnabled();
        const bool useLegacyParticleVfxSnapshotBridge = game::runtime::session_render_config::backendUseLegacyParticleVfxSnapshotBridgeEnabled();

        runtime::ui_inventory_panel::clearHitRegions(backendInventoryPanel);

        auto& scratch = game::runtime::session_render_scratch::threadScratch();
        game::runtime::session_render_scratch::ensureCapacity(scratch);
        auto& worldBackgroundQuads = scratch.worldBackgroundQuads;
        auto& worldQuads = scratch.worldQuads;
        auto& worldTriangles = scratch.worldTriangles;
        auto& world3DTriangles = scratch.world3DTriangles;
        auto& worldIndexedBatches = scratch.worldIndexedBatches;
        auto& overlayQuads = scratch.overlayQuads;
        auto& lines = scratch.lines;
        auto& textLines = scratch.textLines;
        auto& sprites = scratch.sprites;
        std::uint32_t visibleAnimatedUnitsThisFrame = 0u;
        std::uint32_t particleCountThisFrame = 0u;
        float projectedUnitsMsThisFrame = 0.0f;
        float projectedPoseEvalMsThisFrame = 0.0f;
        float projectedModelMsThisFrame = 0.0f;
        float projectedModelPrepMsThisFrame = 0.0f;
        float projectedModelGeometryMsThisFrame = 0.0f;
        float projectedOverlayMsThisFrame = 0.0f;
        std::uint32_t projectedUnitsProcessedThisFrame = 0u;
        std::uint32_t projectedModelUnitsThisFrame = 0u;
        std::uint32_t projectedClipSkinnedUnitsThisFrame = 0u;
        float worldBackdropComposeMsThisFrame = 0.0f;
        float worldVfxBridgeMsThisFrame = 0.0f;
        float worldDepthFlushMsThisFrame = 0.0f;

        const bool supportsWorldTriangles3D = renderer->supportsWorldTriangles3D();
        const bool supportsWorldIndexedMeshes = renderer->supportsWorldIndexedMeshes();
        const bool allowPortraitFallback = game::runtime::session_render_config::backendWorldPortraitFallbackEnabled();
        const bool forcePortraitOverlay = game::runtime::session_render_config::backendWorldPortraitOverlayForced();
        float worldViewProj[16] = {};
        bool hasWorldViewProj = false;
        float cameraWorldPos3[3] = {0.0f, 7.0f, 9.0f};
        float cameraForward3[3] = {0.0f, -0.6139406f, -0.7893522f};
        float cameraTarget3[3] = {0.0f, -1.0f, 0.0f};
        const auto layout =
            game::runtime::session_render_layout::build(config, drawableW, drawableH);

        const runtime::render::RenderRoutes routes = activeRenderRoutes();
        const bool showWorldBackdrop = runtime::render::shouldRenderBackendWorldBackdrop(
            routes,
            renderWorld,
            allowBackendMenuBackdrop);
        const bool useProjectedWorldLayout =
            showWorldBackdrop && renderWorld && gameWorld && (camera != nullptr);

        game::runtime::session_render_scratch::beginFrame(scratch, useProjectedWorldLayout);
        if (showWorldBackdrop) {
            if (useProjectedWorldLayout) {
                const auto projectedWorld =
                    game::runtime::session_projected_world_view::appendProjectedWorldView(
                        {
                            .renderer = renderer,
                            .gameWorld = gameWorld.get(),
                            .camera = camera,
                            .dataDb = &dataDb,
                            .scratch = &scratch,
                            .backendTextureByPath = &backendTextureByPath,
                            .sharedUnitHudCfg = layout.sharedUnitHudCfg,
                            .supportsWorldTriangles3D = supportsWorldTriangles3D,
                            .supportsWorldIndexedMeshes = supportsWorldIndexedMeshes,
                            .characterInkingEnabled =
                                (services ? services->characterInkingEnabled : false),
                            .enableGpuClipSkinning =
                                game::runtime::session_render_config::backendGpuClipSkinningEnabled(renderer),
                            .allowPortraitFallback = allowPortraitFallback,
                            .forcePortraitOverlay = forcePortraitOverlay,
                            .useLegacyGrowlWaveVfx = useLegacyGrowlWaveVfx,
                            .useLegacyParticleVfxSnapshotBridge =
                                useLegacyParticleVfxSnapshotBridge,
                            .useExactTailFireCpuPath =
                                game::runtime::session_render_config::backendUseExactTailFireCpuPathEnabled(),
                            .drawableW = drawableW,
                            .drawableH = drawableH,
                            .rows = layout.rows,
                            .cols = layout.cols,
                            .benchSlots = config.benchSlots,
                            .minDim = layout.minDim,
                            .boardX = layout.boardX,
                            .boardY = layout.boardY,
                            .boardW = layout.boardW,
                            .boardH = layout.boardH,
                            .cellW = layout.cellW,
                            .cellH = layout.cellH,
                            .simNowSec = timeSource.nowSeconds(),
                            .ensureBackendMeshLoaded =
                                [&](const std::string& modelPath) {
                                    return ensureBackendMeshLoaded(modelPath);
                                },
                            .ensureBackendTextureLoaded =
                                [&](const std::string& texturePath, bool flipVertical) {
                                    return ensureBackendTextureLoaded(texturePath, flipVertical);
                                },
                        });
                hasWorldViewProj = projectedWorld.hasWorldViewProj;
                std::copy(
                    projectedWorld.worldViewProj.begin(),
                    projectedWorld.worldViewProj.end(),
                    worldViewProj);
                std::copy(
                    projectedWorld.cameraWorldPos.begin(),
                    projectedWorld.cameraWorldPos.end(),
                    cameraWorldPos3);
                std::copy(
                    projectedWorld.cameraForward.begin(),
                    projectedWorld.cameraForward.end(),
                    cameraForward3);
                std::copy(
                    projectedWorld.cameraTarget.begin(),
                    projectedWorld.cameraTarget.end(),
                    cameraTarget3);
                visibleAnimatedUnitsThisFrame += projectedWorld.visibleAnimatedUnits;
                projectedUnitsMsThisFrame = projectedWorld.projectedUnitsMs;
                projectedPoseEvalMsThisFrame = projectedWorld.projectedPoseEvalMs;
                projectedModelMsThisFrame = projectedWorld.projectedModelMs;
                projectedModelPrepMsThisFrame = projectedWorld.projectedModelPrepMs;
                projectedModelGeometryMsThisFrame =
                    projectedWorld.projectedModelGeometryMs;
                projectedOverlayMsThisFrame = projectedWorld.projectedOverlayMs;
                projectedUnitsProcessedThisFrame =
                    projectedWorld.projectedUnitsProcessed;
                projectedModelUnitsThisFrame = projectedWorld.projectedModelUnits;
                projectedClipSkinnedUnitsThisFrame =
                    projectedWorld.projectedClipSkinnedUnits;
                worldBackdropComposeMsThisFrame =
                    projectedWorld.worldBackdropComposeMs;
                worldVfxBridgeMsThisFrame = projectedWorld.worldVfxBridgeMs;
                worldDepthFlushMsThisFrame = projectedWorld.worldDepthFlushMs;
            } else {
                visibleAnimatedUnitsThisFrame +=
                    game::runtime::session_legacy_world_view::appendLegacyWorldView(
                        {
                            .renderWorld = renderWorld,
                            .gameWorld = gameWorld.get(),
                            .drawableW = drawableW,
                            .drawableH = drawableH,
                            .rows = layout.rows,
                            .cols = layout.cols,
                            .benchSlots = config.benchSlots,
                            .minDim = layout.minDim,
                            .boardX = layout.boardX,
                            .boardY = layout.boardY,
                            .boardW = layout.boardW,
                            .boardH = layout.boardH,
                            .cellW = layout.cellW,
                            .cellH = layout.cellH,
                            .sharedUnitHudCfg = layout.sharedUnitHudCfg,
                        },
                        scratch).visibleAnimatedUnits;
            }
        }
        if (renderWorld && gameWorld) {
            particleCountThisFrame = gameWorld->countActiveParticleVfx();
        }
        if (prewarmWorldIndexedOnly) {
            if (!worldIndexedBatches.empty() && hasWorldViewProj && supportsWorldIndexedMeshes) {
                return runtime::shared_world_batches::prewarmWorldIndexedBatches(
                    *renderer,
                    worldIndexedBatches,
                    cameraWorldPos3,
                    cameraForward3,
                    cameraTarget3);
            }
            return 0u;
        }
        const auto worldComposeEnd = RenderBuildClock::now();
        game::runtime::session_frame_metrics::publish(
            engineServices,
            {
                .visibleAnimatedUnits = visibleAnimatedUnitsThisFrame,
                .particleCount = particleCountThisFrame,
                .projectedUnitsMs = projectedUnitsMsThisFrame,
                .projectedPoseEvalMs = projectedPoseEvalMsThisFrame,
                .projectedModelMs = projectedModelMsThisFrame,
                .projectedModelPrepMs = projectedModelPrepMsThisFrame,
                .projectedModelGeometryMs = projectedModelGeometryMsThisFrame,
                .projectedOverlayMs = projectedOverlayMsThisFrame,
                .projectedUnitsProcessed = projectedUnitsProcessedThisFrame,
                .projectedModelUnits = projectedModelUnitsThisFrame,
                .projectedClipSkinnedUnits = projectedClipSkinnedUnitsThisFrame,
                .worldComposeMs = static_cast<float>(
                    std::chrono::duration<double, std::milli>(
                        worldComposeEnd - worldComposeStart).count()),
                .worldBackdropMs = worldBackdropComposeMsThisFrame,
                .worldVfxMs = worldVfxBridgeMsThisFrame,
                .worldDepthFlushMs = worldDepthFlushMsThisFrame,
            });

        runtime::shared_backend_debug_view::ComposeAndSubmitArgs overlayArgs;
        overlayArgs.renderer = renderer;
        overlayArgs.engineServices = engineServices;
        overlayArgs.services = services.get();
        overlayArgs.gameWorld = gameWorld.get();
        overlayArgs.camera = camera;
        overlayArgs.ecsWorld = &ecsWorld;
        overlayArgs.roundPhaseEntity = roundPhaseEntity;
        overlayArgs.log = &log;
        overlayArgs.backendInventoryPanel = &backendInventoryPanel;
        overlayArgs.refreshBackendInventoryFromWorld = [&]() {
            game::runtime::session_backend_inventory_ui::refreshPanel(
                backendInventoryPanel,
                kBackendInventoryVisibleCount,
                backendInventoryUiDependencies());
        };
        overlayArgs.showPerfOverlay = showPerfOverlay;
        overlayArgs.renderWorld = renderWorld;
        overlayArgs.hasWorldViewProj = hasWorldViewProj;
        overlayArgs.supportsWorldTriangles3D = supportsWorldTriangles3D;
        overlayArgs.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
        overlayArgs.drawableW = drawableW;
        overlayArgs.drawableH = drawableH;
        overlayArgs.edgePad = layout.edgePad;
        overlayArgs.lineStep = layout.lineStep;
        overlayArgs.uiScale = layout.uiScale;
        overlayArgs.worldViewProj = hasWorldViewProj ? worldViewProj : nullptr;
        overlayArgs.renderBuildBreakdown =
            engineServices ? &engineServices->frameRenderBuildBreakdown : nullptr;
        overlayArgs.worldBackgroundQuads = &worldBackgroundQuads;
        overlayArgs.worldQuads = &worldQuads;
        overlayArgs.worldTriangles = &worldTriangles;
        overlayArgs.world3DTriangles = &world3DTriangles;
        overlayArgs.worldIndexedBatches = &worldIndexedBatches;
        overlayArgs.overlayQuads = &overlayQuads;
        overlayArgs.lines = &lines;
        overlayArgs.textLines = &textLines;
        overlayArgs.sprites = &sprites;
        runtime::shared_backend_debug_view::composeAndSubmit(overlayArgs);
        return 0u;
    }

    void renderWorldLayer(int drawableW, int drawableH, bool renderWorld) {
        const runtime::render::RenderRoutes routes = activeRenderRoutes();
        if (routes.usesBackendRenderPath()) {
            (void)renderBackendDebugView(drawableW, drawableH, renderWorld);
        }
    }

    std::size_t prewarmWorldIndexedLayer(int drawableW, int drawableH, bool renderWorld) {
        const runtime::render::RenderRoutes routes = activeRenderRoutes();
        if (!routes.usesBackendRenderPath()) return 0u;
        return renderBackendDebugView(
            drawableW,
            drawableH,
            renderWorld,
            /*prewarmWorldIndexedOnly=*/true);
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








