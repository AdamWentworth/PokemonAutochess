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

#include "engine/core/ecs/Scheduler.h"
#include "engine/core/ecs/World.h"
#include "engine/utils/LogSink.h"

#include "game/GameWorld.h"
#include "game/GameStateManager.h"
#include "game/runtime/routes/RenderFlowDecisions.h"
#include "game/runtime/routes/StartupRenderRoutePolicy.h"
#include "game/runtime/ui/DebugText.h"
#include "game/runtime/routes/GameServiceRenderRoutes.h"
#include "game/runtime/ui/InventoryPanel.h"
#include "game/runtime/ui/InputSlots.h"
#include "game/runtime/startup/RuntimeWorldLayerPrewarm.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/GameServices.h"
#include "game/GameConfig.h"
#include "game/runtime/session/GameUpdateGraph.h"
#include "game/runtime/session/SessionBackendAssetBridge.h"
#include "game/runtime/session/SessionCoreBootstrapRuntime.h"
#include "game/runtime/session/SessionBackendInventoryUi.h"
#include "game/runtime/session/SessionCoordinatorBridge.h"
#include "game/runtime/session/SessionDebugSnapshot.h"
#include "game/runtime/session/SessionInventoryBridge.h"
#include "game/runtime/session/SessionInitBridge.h"
#include "game/runtime/session/SessionLifecycleBridge.h"
#include "game/runtime/session/SessionLoopBridge.h"
#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/session/SessionRenderConfig.h"
#include "game/runtime/session/SessionRenderBridge.h"
#include "game/runtime/session/SessionSnapshotController.h"
#include "game/runtime/session/SessionStartupBridge.h"
#include "game/runtime/session/SessionWorldBackdrop.h"
#include "game/runtime/session/SessionWorldLayerBridge.h"
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

#include "game/logging/LogBus.h"
#include "game/logging/LoggerUtil.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/world/MoveImpactRouting.h"
#include "game/state/scripted/ScriptedState.h"
#include "game/state/CombatState.h"

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
    engine::log::Sink consoleLog{"GameSession", &std::cout, &std::cerr};
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
    game::runtime::session_world_backdrop::Route1BackdropTuningState route1BackdropTuning =
        game::runtime::session_world_backdrop::defaultRoute1BackdropTuningState();
    std::function<void(const std::string&)> setTitleCallback;
    game::runtime::session_loop_runtime::PauseState pauseState;

    static constexpr std::size_t kBackendInventoryVisibleCount = 6;
    runtime::ui_inventory_panel::PanelState backendInventoryPanel;
    runtime::session_backend_asset_bridge::State backendAssets;

    std::shared_ptr<CameraSystem>           cameraSystem;
    std::shared_ptr<UnitInteractionSystem>  unitSystem;
    ShopSystem*                             shopSystem = nullptr;
    RoundSystem*                            roundSystem = nullptr;


    Impl(GameContext& ctx, GameDataDb db)
        : dataDb(std::move(db))
        , ecsWorld(&coreServices) {
        init(ctx);
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
        return game::runtime::session_backend_asset_bridge::ensureBackendMeshLoaded(
            backendAssets,
            modelPath,
            consoleLog);
    }

    game::runtime::SharedBackendTextureCacheEntry* ensureBackendTextureLoaded(
        const std::string& texturePath,
        bool flipVertical = false) {
        return game::runtime::session_backend_asset_bridge::ensureBackendTextureLoaded(
            backendAssets,
            texturePath,
            flipVertical);
    }

    void hydrateBackendUnitAnimationAndScale() {
        game::runtime::session_backend_asset_bridge::hydrateBackendUnitAnimationAndScale(
            backendAssets,
            gameWorld.get(),
            dataDb,
            usesBackendGameRenderPath(),
            consoleLog);
    }

    void init(GameContext& ctx) {
        game::runtime::session_init_bridge::run(
            {
                .ctx = &ctx,
                .camera = &camera,
                .renderer = &renderer,
                .engineServices = &engineServices,
                .setTitleCallback = &setTitleCallback,
                .startupRoutes = &startupRoutes,
                .allowBackendMenuBackdrop = &allowBackendMenuBackdrop,
                .showPerfOverlay = &showPerfOverlay,
                .viewport = &viewport,
                .dataDb = &dataDb,
                .log = &log,
                .consoleLog = &consoleLog,
                .scriptEvents = &scriptEvents,
                .assetStore = &assetStore,
                .rng = &rng,
                .timeSource = &timeSource,
                .config = &config,
                .services = &services,
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
                .backendAssets = &backendAssets,
                .worldLayerPrewarmFramesRemaining = &worldLayerPrewarmFramesRemaining,
                .worldLayerPrewarmFrameCount = kWorldLayerPrewarmFrames,
                .usesBackendGameRenderPath = [&]() { return usesBackendGameRenderPath(); },
                .renderWorldLayer =
                    [&](int drawableW, int drawableH, bool renderWorld) {
                        game::runtime::session_coordinator_bridge::renderWorldLayer(
                            coordinatorContext(),
                            drawableW,
                            drawableH,
                            renderWorld);
                    },
                .maybeAutoLoadSnapshot =
                    [&]() {
                        game::runtime::session_coordinator_bridge::maybeAutoLoadDebugStateSnapshot(
                            coordinatorContext());
                    },
                .snapshotPath = debugStateSnapshotPath(),
                .autoLoadSnapshotOnStartup =
                    game::runtime::session_debug_snapshot::autoLoadSnapshotEnabled(),
            });
    }

    game::runtime::session_coordinator_bridge::Context coordinatorContext() {
        return game::runtime::session_coordinator_bridge::Context{
            .snapshotPath = debugStateSnapshotPath(),
            .autoLoadSnapshotOnStartup =
                game::runtime::session_debug_snapshot::autoLoadSnapshotEnabled(),
            .log = &log,
            .consoleLog = &consoleLog,
            .pauseState = &pauseState,
            .engineServices = engineServices,
            .viewport = &viewport,
            .unitSystem = unitSystem.get(),
            .cameraSystem = cameraSystem.get(),
            .stateManager = stateManager.get(),
            .gameWorld = gameWorld.get(),
            .services = services.get(),
            .roundSystem = roundSystem,
            .roundSystemRef = &roundSystem,
            .backendInventoryPanel = &backendInventoryPanel,
            .backendInventoryVisibleCount = kBackendInventoryVisibleCount,
            .renderWorldForInput =
                game::runtime::session_loop_runtime::renderWorldForInput(stateManager.get()),
            .usesBackendGameUiPath = usesBackendGameUiPath(),
            .usesBackendGameRenderPath = usesBackendGameRenderPath(),
            .advanceTime = [&](float deltaTime) { timeSource.advance(deltaTime); },
            .hydrateBackend = [&]() { hydrateBackendUnitAnimationAndScale(); },
            .tickUpdateGraph = [&](float deltaTime) { updateGraph.tick(deltaTime); },
            .renderer = renderer,
            .camera = camera,
            .ecsWorld = &ecsWorld,
            .roundPhaseEntity = roundPhaseEntity,
            .config = &config,
            .dataDb = &dataDb,
            .backendTextureByPath =
                &game::runtime::session_backend_asset_bridge::textureCache(backendAssets),
            .routes = activeRenderRoutes(),
            .showPerfOverlay = showPerfOverlay,
            .enableBackdropTiles =
                engineServices ? engineServices->sessionBackdropTilesEnabled : true,
            .allowBackendMenuBackdrop = allowBackendMenuBackdrop,
            .simNowSec = timeSource.nowSeconds(),
            .route1BackdropTuning = &route1BackdropTuning,
            .ensureBackendMeshLoaded =
                [&](const std::string& modelPath) {
                    return ensureBackendMeshLoaded(modelPath);
                },
            .ensureBackendTextureLoaded =
                [&](const std::string& texturePath, bool flipVertical) {
                    return ensureBackendTextureLoaded(texturePath, flipVertical);
                },
            .worldLayerPrewarmFramesRemaining = &worldLayerPrewarmFramesRemaining,
            .worldLayerPrewarmFrameCount = kWorldLayerPrewarmFrames,
            .setUnitScreenSize =
                [&](unsigned int drawableW, unsigned int drawableH) {
                    if (unitSystem) {
                        unitSystem->setScreenSize(drawableW, drawableH);
                    }
                },
            .resolveRenderWorld =
                [&]() {
                    if (stateManager) {
                        if (auto* state = stateManager->getCurrentState()) {
                            return state->shouldRenderWorld();
                        }
                    }
                    return true;
                },
            .currentFrameFlow =
                [&](bool renderWorld) {
                    return currentFrameFlow(renderWorld);
                },
            .setTitle = setTitleCallback,
            .renderStateLayer =
                [&]() {
                    if (stateManager) {
                        stateManager->render();
                    }
                },
            .resetRenderCaches =
                [&]() {
                    game::runtime::session_render_scratch::resetSceneCaches(
                        game::runtime::session_render_scratch::threadScratch());
                },
            .shopSystem = &shopSystem,
            .unitSystemRef = &unitSystem,
            .cameraSystemRef = &cameraSystem,
            .stateManagerRef = &stateManager,
            .gameWorldRef = &gameWorld,
            .scheduler = &scheduler,
        };
    }

    void handleEvent(const InputEvent& event) {
        game::runtime::session_coordinator_bridge::handleEvent(event, coordinatorContext());
    }

    void fixedUpdate(float dt) {
        game::runtime::session_coordinator_bridge::fixedUpdate(dt, coordinatorContext());
    }

    void render(int drawableW, int drawableH) {
        game::runtime::session_coordinator_bridge::render(
            drawableW,
            drawableH,
            coordinatorContext());
    }

    bool activateEditorPreview(
        const std::string& state,
        const std::string& gameMode,
        const std::string& snapshotPath,
        std::string* outError) {
        if (!stateManager || !gameWorld || !services) {
            if (outError) {
                *outError =
                    "Game session is not ready for editor preview switching.";
            }
            return false;
        }
        if (gameMode != "classic" &&
            gameMode != "adventure") {
            if (outError) {
                *outError =
                    "Unsupported editor preview game mode: " +
                    gameMode;
            }
            return false;
        }

        services->gameMode = gameMode;
        if (state == "main_menu" || state == "starter") {
            const int startingMoney =
                gameMode == "classic"
                    ? config.classicStartingGold
                    : config.startingCash;
            gameWorld->resetForNewGame(startingMoney);
            gameWorld->setUnitSellRewardsEnabled(
                gameMode == "classic");
            const char* script =
                state == "starter"
                    ? "scripts/states/starter.lua"
                    : "scripts/states/main_menu.lua";
            stateManager->clearAndPushState(
                std::make_unique<ScriptedState>(
                    stateManager.get(),
                    gameWorld.get(),
                    *services,
                    engine::paths::data(script)));
            game::runtime::session_render_scratch::
                resetSceneCaches(
                    game::runtime::session_render_scratch::
                        threadScratch());
            if (outError) {
                outError->clear();
            }
            return true;
        }

        if (state == "snapshot") {
            if (snapshotPath.empty() ||
                !std::filesystem::is_regular_file(
                    snapshotPath)) {
                if (outError) {
                    *outError =
                        "Editor preview snapshot does not exist: " +
                        snapshotPath;
                }
                return false;
            }
            auto context = coordinatorContext();
            context.snapshotPath = snapshotPath;
            context.renderer = nullptr;
            game::runtime::session_coordinator_bridge::
                loadDebugStateSnapshot(context);
            services->gameMode = gameMode;
            gameWorld->setUnitSellRewardsEnabled(
                gameMode == "classic");
            if (outError) {
                outError->clear();
            }
            return true;
        }

        if (state == "route") {
            if (snapshotPath.empty() ||
                !std::filesystem::is_regular_file(
                    snapshotPath)) {
                if (outError) {
                    *outError =
                        "Editor route script does not exist: " +
                        snapshotPath;
                }
                return false;
            }
            const int startingMoney =
                gameMode == "classic"
                    ? config.classicStartingGold
                    : config.startingCash;
            gameWorld->resetForNewGame(startingMoney);
            gameWorld->setUnitSellRewardsEnabled(
                gameMode == "classic");
            gameWorld->spawnPokemonAtGrid(
                "bulbasaur",
                3,
                6,
                PokemonSide::Player,
                5);
            stateManager->clearAndPushState(
                std::make_unique<CombatState>(
                    stateManager.get(),
                    gameWorld.get(),
                    *services,
                    snapshotPath,
                    false));
            game::runtime::session_render_scratch::
                resetSceneCaches(
                    game::runtime::session_render_scratch::
                        threadScratch());
            if (outError) {
                outError->clear();
            }
            return true;
        }

        if (outError) {
            *outError =
                "Unsupported editor preview state: " + state;
        }
        return false;
    }

    void shutdown() {
        game::runtime::session_coordinator_bridge::shutdown(coordinatorContext());
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
bool GameSession::activateEditorPreview(
    const std::string& state,
    const std::string& gameMode,
    const std::string& snapshotPath,
    std::string* outError) {
    return impl_->activateEditorPreview(
        state,
        gameMode,
        snapshotPath,
        outError);
}
void GameSession::shutdown() { impl_->shutdown(); }

} // namespace game








