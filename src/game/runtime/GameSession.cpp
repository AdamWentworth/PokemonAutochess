#include "game/runtime/GameSession.h"

// Heavy includes live here (not in headers).
#include <iostream>
#include <string>
#include <utility>
#include <cctype>
#include <cstdint>
#include <random>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/core/GameContext.h"
#include "engine/core/EngineServices.h"
#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "engine/core/Services.h"
#include "engine/core/TimeSources.h"
#include "engine/core/Environment.h"
#include "engine/core/ecs/Entity.h"
#include "engine/input/InputEvent.h"

#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"
#include "engine/render/IRenderBackend.h"

#include "engine/ui/UIManager.h"
#include "engine/ui/BattleFeed.h"
#include "engine/ui/HealthBarRenderer.h"
#include "engine/ui/TextRenderer.h"

#include "engine/core/ecs/Scheduler.h"
#include "engine/core/ecs/World.h"

#include "game/GameWorld.h"
#include "game/GameStateManager.h"
#include "game/runtime/GamePreload.h"
#include "game/runtime/RenderFlowDecisions.h"
#include "game/runtime/BackendDebugText.h"
#include "game/runtime/BackendInventoryOverlay.h"
#include "game/runtime/BackendInventoryPanel.h"
#include "game/runtime/BackendInputSlots.h"
#include "game/runtime/BackendStatusText.h"
#include "game/runtime/BackendUiScale.h"
#include "game/runtime/BackendHudFormatting.h"
#include "game/runtime/BackendWorldProjection.h"
#include "game/runtime/BackendWorldProxyGeometry.h"
#include "game/runtime/BackendModelCache.h"
#include "game/runtime/BackendProceduralPose.h"
#include "game/runtime/BackendUnitVisuals.h"
#include "game/GameServices.h"
#include "game/GameConfig.h"
#include "game/runtime/GameUpdateGraph.h"
#include "game/ui/UIViewport.h"
#include "game/ui/ItemInventoryUI.h"
#include "game/ui/ShopLayout.h"

#include "game/config/GameDataDb.h"
#include "game/assets/DevAssetStore.h"
#include "game/assets/PackedAssetStore.h"
#include "game/ecs/RoundState.h"
#include "game/ecs/CombatActive.h"

#include "game/systems/CameraSystem.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/MovementSystem.h"
#include "game/systems/CombatSystem.h"
#include "game/systems/ShopSystem.h"
#include "game/systems/LegacySystemAdapters.h"

#include "game/state/ScriptedState.h"
#include "game/logging/LogBus.h"
#include "game/logging/LoggerUtil.h"
#include "game/scripting/ScriptEventBus.h"

namespace {
std::string trimDebugLine(std::string s, std::size_t maxChars) {
    if (s.size() <= maxChars) return s;
    if (maxChars <= 3) return s.substr(0, maxChars);
    return s.substr(0, maxChars - 3) + "...";
}

std::size_t backendModelTriangleLimit() {
    static const std::size_t limit = []() -> std::size_t {
        constexpr std::size_t kDefault = 24000u;
        constexpr std::size_t kMin = 256u;
        constexpr std::size_t kMax = 120000u;
        const auto env = engine::env::get("PAC_BACKEND_MODEL_TRI_LIMIT");
        if (!env.has_value()) return kDefault;
        try {
            const std::size_t parsed = static_cast<std::size_t>(std::stoull(*env));
            return std::clamp(parsed, kMin, kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return limit;
}

std::size_t selectUniformTriangleIndex(std::size_t sampleIndex,
                                       std::size_t sampleCount,
                                       std::size_t triangleCount) {
    if (sampleCount == 0u || triangleCount == 0u) return 0u;
    if (sampleCount >= triangleCount) return std::min(sampleIndex, triangleCount - 1u);
    const double span = static_cast<double>(triangleCount) / static_cast<double>(sampleCount);
    const double center = (static_cast<double>(sampleIndex) + 0.5) * span;
    const std::size_t tri =
        std::min(triangleCount - 1u, static_cast<std::size_t>(std::floor(center)));
    return tri;
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
    std::unique_ptr<BoardRenderer>    board;
    std::unique_ptr<BattleFeed>       battleFeed;
    std::unique_ptr<BattleFeed>       catchFeed;
    std::unique_ptr<BattleFeed>       economyFeed;
    std::unique_ptr<TextRenderer>     typeBonusText;
    ItemInventoryUI                  itemInventoryUI;

    HealthBarRenderer healthBarRenderer;
    engine::ecs::Scheduler scheduler;
    GameUpdateGraph updateGraph;

    bool renderEnabled = false;
    bool legacyRenderPath = false;
    bool showPerfOverlay = true;
    bool devPauseWorld = false;
    int devPauseStepTicks = 0;

    static constexpr std::size_t kBackendInventoryVisibleCount = 6;
    runtime::backend_inventory_panel::PanelState backendInventoryPanel;
    struct BackendMeshCacheEntry {
        bool attemptedLoad = false;
        runtime::backend_model::MeshData mesh;
        std::string error;
    };
    std::unordered_map<std::string, BackendMeshCacheEntry> backendMeshByModelPath;

    std::shared_ptr<CameraSystem>           cameraSystem;
    std::shared_ptr<UnitInteractionSystem>  unitSystem;
    ShopSystem*                             shopSystem = nullptr;


    Impl(GameContext& ctx, GameDataDb db)
        : dataDb(std::move(db))
        , ecsWorld(&coreServices) {
        init(ctx);
    }

    void init(GameContext& ctx) {
        camera = ctx.camera;
        renderer = ctx.renderer;
        engineServices = ctx.services;
        const bool hasBackend = (ctx.renderer != nullptr) && (ctx.camera != nullptr);
        legacyRenderPath = hasBackend && std::string(ctx.renderer->backendId()) == "opengl";
        renderEnabled = hasBackend;
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
                                                  &ecsWorld, roundPhaseEntity, &viewport, legacyRenderPath);
        services->renderer = renderer;
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
            services->requireDiscreteGpu = ctx.services->requireDiscreteGpu;
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

        // Board visuals
        if (legacyRenderPath) {
            board = std::make_unique<BoardRenderer>(config.rows, config.cols, config.cellSize);
        }

        // World
        gameWorld = std::make_unique<GameWorld>(config);
        gameWorld->setRenderEnabled(legacyRenderPath);
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

        if (cameraSystem) scheduler.add(std::make_unique<game::UpdatableSystemAdapter>(cameraSystem.get()), Phase::Update);
        if (unitSystem)   scheduler.add(std::make_unique<game::UpdatableSystemAdapter>(unitSystem.get()), Phase::Update);
        auto shopSystemImpl = std::make_unique<ShopSystem>(services->rng);
        shopSystem = shopSystemImpl.get();
        scheduler.add(std::move(shopSystemImpl), Phase::Update);

        auto roundSystem = std::make_unique<RoundSystem>(*services, roundPhaseEntity);
        ecsWorld.add<game::RoundState>(roundPhaseEntity, game::RoundState{ roundSystem->getCurrentPhase() });
        scheduler.add(std::move(roundSystem), Phase::Update);


        if (legacyRenderPath) {
            if (ctx.services && ctx.services->shaders) {
                healthBarRenderer.init(*ctx.services->shaders);
            } else {
                healthBarRenderer.init();
            }
            const int levelFontSize = std::max(20, config.fontSize / 2);
            healthBarRenderer.setFont(config.fontPath, levelFontSize,
                                      ctx.services ? ctx.services->shaders : nullptr);

            // Battle feed + logger (instance-based)
            battleFeed = std::make_unique<BattleFeed>(config.fontPath, config.fontSize);
            battleFeed->setAlignRight(true);
            battleFeed->setBaseScale(0.40f);
            log.attach(battleFeed.get());
            log.setEchoToStdout(true);

            // Catch feed (right side)
            catchFeed = std::make_unique<BattleFeed>(config.fontPath, config.fontSize);
            catchFeed->setAlignRight(false);
            catchFeed->setWrapWidth(320.f);
            catchFeed->setMaxLines(5);
            catchFeed->setBaseScale(0.38f);
            catchFeed->setPadding(16.f, 16.f);
            log.attachCatchFeed(catchFeed.get());

            // Classic economy feed (bottom-right, mode-specific)
            economyFeed = std::make_unique<BattleFeed>(config.fontPath, config.fontSize);
            economyFeed->setAlignRight(false);
            economyFeed->setWrapWidth(320.f);
            economyFeed->setMaxLines(4);
            economyFeed->setBaseScale(0.36f);
            economyFeed->setPadding(16.f, 16.f);
            log.attachEconomyFeed(economyFeed.get());

            itemInventoryUI.init(config.fontPath, config.fontSize);
            typeBonusText = std::make_unique<TextRenderer>(config.fontPath, std::max(18, config.fontSize / 2));
        }

        if (auto* stateMgr = stateManager.get()) {
            scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
                [stateMgr](float dt) { stateMgr->update(dt); }
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
                [worldPtr](float dt) { worldPtr->update(dt); }
            ), Phase::PostUpdate);
        }
        if (auto* feed = battleFeed.get()) {
            scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
                [feed](float dt) { feed->update(dt); }
            ), Phase::PostUpdate);
        }
        if (auto* feed = catchFeed.get()) {
            scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
                [feed](float dt) { feed->update(dt); }
            ), Phase::PostUpdate);
        }
        if (auto* feed = economyFeed.get()) {
            scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
                [feed](float dt) { feed->update(dt); }
            ), Phase::PostUpdate);
        }

        updateGraph.configure({
            &scheduler,
            &ecsWorld,
            roundPhaseEntity,
            shopSystem,
            &log,
            &scriptEvents
        });

        // Preload common models (uses the db's pokemon loader).
        if (legacyRenderPath) {
            game::preload::preloadCommonModels(ctx, dataDb.pokemon, "PokemonAutochess");
        } else {
            std::cout << "[Init] Non-OpenGL render path: skipping GL model preload.\n";
        }

        stateManager->pushState(std::make_unique<ScriptedState>(
            stateManager.get(),
            gameWorld.get(),
            *services,
            engine::paths::data("scripts/states/main_menu.lua")
        ));

        if (ctx.setTitle) ctx.setTitle("Pokemon Autochess");
        std::cout << "[Init] Game initialized.\n";
    }

    bool selectBackendInventoryItem(const std::string& itemId) {
        if (!gameWorld || itemId.empty()) return false;
        if (gameWorld->getSelectedItem() == itemId) return true;
        gameWorld->setSelectedItem(itemId);
        log.catchInfo("Selected " + runtime::hud::humanizeToken(itemId) + ". Click a target.");
        return true;
    }

    bool clearBackendInventorySelection() {
        if (!gameWorld) return false;
        if (gameWorld->getSelectedItem().empty()) return false;
        gameWorld->setSelectedItem("");
        log.catchInfo("Cleared selected item.");
        return true;
    }

    void refreshBackendInventoryFromWorld() {
        if (!gameWorld) {
            backendInventoryPanel = {};
            return;
        }

        runtime::backend_inventory_panel::refreshPanelState(
            backendInventoryPanel,
            gameWorld->listItems(),
            kBackendInventoryVisibleCount,
            gameWorld->getSelectedItem());
    }

    bool applyBackendInventoryOffsetDelta(int delta) {
        if (delta == 0 || !gameWorld) return false;

        refreshBackendInventoryFromWorld();
        return runtime::backend_inventory_panel::applyOffsetDelta(
            backendInventoryPanel,
            delta,
            kBackendInventoryVisibleCount,
            gameWorld->getSelectedItem());
    }

    void handleEvent(const InputEvent& event) {
        if (event.type == InputEvent::Type::Resize) {
            viewport.set(event.drawableW, event.drawableH);
            if (unitSystem) {
                unitSystem->setScreenSize(
                    static_cast<unsigned int>(std::max(1, event.drawableW)),
                    static_cast<unsigned int>(std::max(1, event.drawableH)));
            }
        }

        if (event.type == InputEvent::Type::KeyDown && !event.repeat) {
            if (event.keyId == InputEvent::Key::P) {
                devPauseWorld = !devPauseWorld;
                devPauseStepTicks = 0;
                game::log::info(
                    &log,
                    devPauseWorld
                        ? "[DevPause] ON (P resumes, O steps one frame)"
                        : "[DevPause] OFF");
                return;
            }
            if (event.keyId == InputEvent::Key::O && devPauseWorld) {
                devPauseStepTicks = 1;
                game::log::info(&log, "[DevPause] Step 1 frame");
                return;
            }
        }

        bool renderWorldForInput = true;
        if (stateManager) {
            if (auto* state = stateManager->getCurrentState()) {
                renderWorldForInput = state->shouldRenderWorld();
            }
        }

        if (event.type == InputEvent::Type::KeyDown &&
            event.keyId == InputEvent::Key::Escape &&
            !event.repeat) {
            if (renderWorldForInput && stateManager) {
                stateManager->pushState(std::make_unique<ScriptedState>(
                    stateManager.get(),
                    gameWorld.get(),
                    *services,
                    engine::paths::data("scripts/states/main_menu.lua")
                ));
                return;
            }
        }

        if (renderWorldForInput &&
            event.type == InputEvent::Type::KeyDown &&
            !event.repeat &&
            runtime::backend_input::isClearSelectionKey(event.keyId)) {
            if (clearBackendInventorySelection()) {
                return; // consume key so gameplay actions do not fire simultaneously.
            }
        }

        if (renderWorldForInput &&
            !legacyRenderPath &&
            event.type == InputEvent::Type::KeyDown &&
            !event.repeat) {
            const int offsetDelta = runtime::backend_input::inventoryOffsetDeltaFromKey(
                event.keyId,
                static_cast<int>(kBackendInventoryVisibleCount));
            if (applyBackendInventoryOffsetDelta(offsetDelta)) {
                return; // consume nav key when inventory paging changed.
            }

            refreshBackendInventoryFromWorld();
            const int slot = runtime::backend_input::slotFromNumberKey(event.keyId);
            const auto itemId = runtime::backend_inventory_panel::visibleItemForSlot(
                backendInventoryPanel,
                slot);
            if (itemId && selectBackendInventoryItem(*itemId)) {
                return; // consume key to avoid accidental board interactions.
            }
        }

        if (renderWorldForInput && event.type == InputEvent::Type::MouseWheel) {
            if (legacyRenderPath) {
                itemInventoryUI.handleScroll(event.wheelY, viewport.height);
            } else {
                const int wheelDelta = runtime::backend_inventory_panel::offsetDeltaFromWheel(event.wheelY);
                if (applyBackendInventoryOffsetDelta(wheelDelta)) {
                    return;
                }
            }
        }
        if (renderWorldForInput && event.type == InputEvent::Type::MouseDown &&
            event.mouseButtonId == InputEvent::MouseButton::Left) {
            if (legacyRenderPath) {
                if (auto clicked = itemInventoryUI.handleMouseClick(event.mouseX, event.mouseY)) {
                    if (gameWorld) {
                        gameWorld->setSelectedItem(*clicked);
                        log.catchInfo("Selected " + runtime::hud::humanizeToken(*clicked) + ". Click a target.");
                    }
                    return; // consume click (avoid dragging/other UI)
                }
            } else {
                const float mx = static_cast<float>(event.mouseX);
                const float my = static_cast<float>(event.mouseY);
                const auto* hit = runtime::backend_inventory_panel::findHit(backendInventoryPanel, mx, my);
                if (hit) {
                    if (hit->action == runtime::backend_inventory_panel::HitAction::ClearSelection) {
                        clearBackendInventorySelection();
                        return;
                    }
                    if (hit->action == runtime::backend_inventory_panel::HitAction::ScrollOffset) {
                        applyBackendInventoryOffsetDelta(hit->offsetDelta);
                        return;
                    }
                    if (selectBackendInventoryItem(hit->itemId)) {
                        return;
                    }
                }
            }
        }
        if (renderWorldForInput && cameraSystem) cameraSystem->handleInput(event);
        if (renderWorldForInput && unitSystem)   unitSystem->handleInput(event);
        if (stateManager) stateManager->handleInput(event);
    }

    void fixedUpdate(float dt) {
        if (devPauseWorld && devPauseStepTicks <= 0) {
            return;
        }
        timeSource.advance(dt);
        updateGraph.tick(dt);
        if (devPauseWorld && devPauseStepTicks > 0) {
            --devPauseStepTicks;
        }
    }

    void renderBackendDebugView(int drawableW, int drawableH, bool renderWorld) {
        if (!renderer || drawableW <= 0 || drawableH <= 0) return;

        runtime::backend_inventory_panel::clearHitRegions(backendInventoryPanel);

        std::vector<IRenderBackend::DebugQuad> worldBackgroundQuads;
        worldBackgroundQuads.reserve(1024);
        std::vector<IRenderBackend::DebugQuad> worldQuads;
        worldQuads.reserve(1024);
        std::vector<IRenderBackend::DebugTriangle> worldTriangles;
        worldTriangles.reserve(4096);
        std::vector<IRenderBackend::WorldTriangle> world3DTriangles;
        world3DTriangles.reserve(120000);
        std::vector<IRenderBackend::DebugQuad> overlayQuads;
        overlayQuads.reserve(1024);
        std::vector<IRenderBackend::DebugLine> lines;
        lines.reserve(512);
        std::vector<IRenderBackend::DebugSprite> sprites;
        sprites.reserve(256);
        struct BackendUnitLabel {
            float x = 0.0f;
            float y = 0.0f;
            std::string text;
            glm::vec3 color{1.0f, 1.0f, 1.0f};
        };
        std::vector<BackendUnitLabel> unitLabels;
        unitLabels.reserve(64);
        const bool supportsWorldTriangles3D = renderer->supportsWorldTriangles3D();
        float worldViewProj[16] = {};
        bool hasWorldViewProj = false;

        const int rows = std::max(1, config.rows);
        const int cols = std::max(1, config.cols);
        const float minDim = static_cast<float>(std::min(drawableW, drawableH));
        const float uiScale = runtime::backend_ui::viewportScale(drawableW, drawableH);
        const float edgePad = runtime::backend_ui::edgePad(drawableW, drawableH);
        const float lineStep = runtime::backend_ui::lineStep(drawableW, drawableH);
        const float boardW = std::max(240.0f, minDim * 0.78f);
        const float boardH = std::max(180.0f, minDim * 0.58f);
        const float boardX = (static_cast<float>(drawableW) - boardW) * 0.5f;
        const float boardY = (static_cast<float>(drawableH) - boardH) * 0.5f;
        const float cellW = boardW / static_cast<float>(cols);
        const float cellH = boardH / static_cast<float>(rows);

        const bool showWorldBackdrop = renderWorld || (services && services->activeRendererBackend != "opengl");
        if (showWorldBackdrop) {
            const bool useProjectedWorldLayout = renderWorld && gameWorld && (camera != nullptr);
            if (useProjectedWorldLayout) {
                const float worldCellSize = std::max(0.05f, gameWorld->getBoardCellSize());
                const runtime::backendview::BoardBounds boardBounds =
                    runtime::backendview::computeBoardBounds(cols, rows, worldCellSize);
                const float boardMinX = boardBounds.minX;
                const float boardMinZ = boardBounds.minZ;
                const float boardMaxX = boardBounds.maxX;
                const float boardMaxZ = boardBounds.maxZ;

                const glm::mat4 view = camera->getViewMatrix();
                const glm::mat4 proj = camera->getProjectionMatrix();
                const glm::mat4 viewProj = proj * view;
                if (supportsWorldTriangles3D) {
                    const float* vp = glm::value_ptr(viewProj);
                    std::copy(vp, vp + 16, worldViewProj);
                    hasWorldViewProj = true;
                }
                const glm::mat4 invView = glm::inverse(view);
                glm::vec3 cameraWorldPos(invView[3].x, invView[3].y, invView[3].z);
                if (!std::isfinite(cameraWorldPos.x) ||
                    !std::isfinite(cameraWorldPos.y) ||
                    !std::isfinite(cameraWorldPos.z)) {
                    cameraWorldPos = glm::vec3(0.0f, 6.0f, -6.0f);
                }
                const glm::vec4 screenViewport(
                    0.0f,
                    0.0f,
                    static_cast<float>(drawableW),
                    static_cast<float>(drawableH));

                const auto projectWorld = [&](const glm::vec3& worldPos,
                                              float& outX,
                                              float& outY,
                                              float& outZ) {
                    const glm::vec3 p = glm::project(worldPos, view, proj, screenViewport);
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
                    outX = p.x;
                    outY = static_cast<float>(drawableH) - p.y;
                    outZ = p.z;
                    return true;
                };

                float projMinX = static_cast<float>(drawableW);
                float projMinY = static_cast<float>(drawableH);
                float projMaxX = 0.0f;
                float projMaxY = 0.0f;
                bool hasProjectedBounds = false;
                const glm::vec3 corners[4] = {
                    {boardMinX, 0.01f, boardMinZ},
                    {boardMaxX, 0.01f, boardMinZ},
                    {boardMaxX, 0.01f, boardMaxZ},
                    {boardMinX, 0.01f, boardMaxZ}
                };
                for (const glm::vec3& corner : corners) {
                    float sx = 0.0f;
                    float sy = 0.0f;
                    float sz = 0.0f;
                    if (!projectWorld(corner, sx, sy, sz)) continue;
                    if (sz < 0.0f || sz > 1.0f) continue;
                    projMinX = std::min(projMinX, sx);
                    projMinY = std::min(projMinY, sy);
                    projMaxX = std::max(projMaxX, sx);
                    projMaxY = std::max(projMaxY, sy);
                    hasProjectedBounds = true;
                }
                if (hasProjectedBounds) {
                    IRenderBackend::DebugQuad boardBg;
                    boardBg.x = std::max(0.0f, projMinX);
                    boardBg.y = std::max(0.0f, projMinY);
                    boardBg.w = std::max(0.0f, std::min(static_cast<float>(drawableW), projMaxX) - boardBg.x);
                    boardBg.h = std::max(0.0f, std::min(static_cast<float>(drawableH), projMaxY) - boardBg.y);
                    boardBg.r = 0.07f;
                    boardBg.g = 0.11f;
                    boardBg.b = 0.14f;
                    boardBg.a = 0.36f;
                    worldBackgroundQuads.push_back(boardBg);
                }

                const float line = std::max(1.0f, minDim * 0.0019f);
                const auto appendWorldTriangle = [&](const glm::vec3& a,
                                                     const glm::vec3& b,
                                                     const glm::vec3& c,
                                                     float r,
                                                     float g,
                                                     float bl,
                                                     float alpha) {
                    if (!supportsWorldTriangles3D) return;
                    IRenderBackend::WorldTriangle tri;
                    tri.x1 = a.x;
                    tri.y1 = a.y;
                    tri.z1 = a.z;
                    tri.x2 = b.x;
                    tri.y2 = b.y;
                    tri.z2 = b.z;
                    tri.x3 = c.x;
                    tri.y3 = c.y;
                    tri.z3 = c.z;
                    tri.r = r;
                    tri.g = g;
                    tri.b = bl;
                    tri.a = alpha;
                    world3DTriangles.push_back(tri);
                };
                const auto appendWorldQuad = [&](const glm::vec3& a,
                                                 const glm::vec3& b,
                                                 const glm::vec3& c,
                                                 const glm::vec3& d,
                                                 float r,
                                                 float g,
                                                 float bl,
                                                 float alpha) {
                    appendWorldTriangle(a, b, c, r, g, bl, alpha);
                    appendWorldTriangle(a, c, d, r, g, bl, alpha);
                };
                const auto appendProjectedTriangle = [&](const glm::vec3& a,
                                                         const glm::vec3& b,
                                                         const glm::vec3& c,
                                                         float r,
                                                         float g,
                                                         float bl,
                                                         float alpha) {
                    float x1 = 0.0f;
                    float y1 = 0.0f;
                    float z1 = 0.0f;
                    float x2 = 0.0f;
                    float y2 = 0.0f;
                    float z2 = 0.0f;
                    float x3 = 0.0f;
                    float y3 = 0.0f;
                    float z3 = 0.0f;
                    if (!projectWorld(a, x1, y1, z1) ||
                        !projectWorld(b, x2, y2, z2) ||
                        !projectWorld(c, x3, y3, z3)) {
                        return;
                    }
                    if ((z1 < 0.0f || z1 > 1.0f) &&
                        (z2 < 0.0f || z2 > 1.0f) &&
                        (z3 < 0.0f || z3 > 1.0f)) {
                        return;
                    }
                    IRenderBackend::DebugTriangle tri;
                    tri.x1 = x1;
                    tri.y1 = y1;
                    tri.x2 = x2;
                    tri.y2 = y2;
                    tri.x3 = x3;
                    tri.y3 = y3;
                    tri.r = r;
                    tri.g = g;
                    tri.b = bl;
                    tri.a = alpha;
                    worldTriangles.push_back(tri);
                };
                const auto appendProjectedQuad = [&](const glm::vec3& a,
                                                     const glm::vec3& b,
                                                     const glm::vec3& c,
                                                     const glm::vec3& d,
                                                     float r,
                                                     float g,
                                                     float bl,
                                                     float alpha) {
                    appendProjectedTriangle(a, b, c, r, g, bl, alpha);
                    appendProjectedTriangle(a, c, d, r, g, bl, alpha);
                };
                const auto appendProjectedLine = [&](const glm::vec3& a,
                                                     const glm::vec3& b,
                                                     float r,
                                                     float g,
                                                     float bl,
                                                     float alpha,
                                                     float thickness) {
                    float x1 = 0.0f;
                    float y1 = 0.0f;
                    float z1 = 0.0f;
                    float x2 = 0.0f;
                    float y2 = 0.0f;
                    float z2 = 0.0f;
                    if (!projectWorld(a, x1, y1, z1) || !projectWorld(b, x2, y2, z2)) return;
                    if ((z1 < 0.0f || z1 > 1.0f) && (z2 < 0.0f || z2 > 1.0f)) return;
                    IRenderBackend::DebugLine l;
                    l.x1 = x1;
                    l.y1 = y1;
                    l.x2 = x2;
                    l.y2 = y2;
                    l.thickness = thickness;
                    l.r = r;
                    l.g = g;
                    l.b = bl;
                    l.a = alpha;
                    lines.push_back(l);
                };
                const auto resolveModelMesh = [&](const PokemonInstance& unit)
                    -> const runtime::backend_model::MeshData* {
                    const PokemonStats* stats = dataDb.pokemon.getStats(unit.name);
                    if (!stats || stats->model.empty()) return nullptr;

                    const std::string modelPath = "assets/models/" + stats->model;
                    auto& cacheEntry = backendMeshByModelPath[modelPath];
                    if (!cacheEntry.attemptedLoad) {
                        cacheEntry.attemptedLoad = true;
                        std::string err;
                        if (!runtime::backend_model::loadMeshFromCache(modelPath, cacheEntry.mesh, &err)) {
                            cacheEntry.error = std::move(err);
                            cacheEntry.mesh = {};
                        }
                    }
                    if (cacheEntry.mesh.vertices.empty() || cacheEntry.mesh.indices.size() < 3) {
                        return nullptr;
                    }
                    return &cacheEntry.mesh;
                };
                const std::size_t boardTrianglesStart2D = worldTriangles.size();
                const std::size_t boardTrianglesStart3D = world3DTriangles.size();
                struct DepthTri {
                    IRenderBackend::DebugTriangle tri;
                    float depth = 0.0f;
                };
                std::vector<DepthTri> modelDepthTris;
                modelDepthTris.reserve(12000);

                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        const float x0 = boardMinX + static_cast<float>(c) * worldCellSize;
                        const float z0 = boardMinZ + static_cast<float>(r) * worldCellSize;
                        const float x1 = x0 + worldCellSize;
                        const float z1 = z0 + worldCellSize;
                        const bool darkCell = ((r + c) % 2) == 0;
                        const float cr = darkCell ? 0.08f : 0.12f;
                        const float cg = darkCell ? 0.13f : 0.17f;
                        const float cb = darkCell ? 0.18f : 0.23f;
                        const float ca = darkCell ? 0.34f : 0.27f;
                        const glm::vec3 qa(x0, 0.006f, z0);
                        const glm::vec3 qb(x1, 0.006f, z0);
                        const glm::vec3 qc(x1, 0.006f, z1);
                        const glm::vec3 qd(x0, 0.006f, z1);
                        if (supportsWorldTriangles3D) {
                            appendWorldQuad(qa, qb, qc, qd, cr, cg, cb, ca);
                        } else {
                            appendProjectedQuad(qa, qb, qc, qd, cr, cg, cb, ca);
                        }
                    }
                }

                for (int c = 0; c <= cols; ++c) {
                    const float x = boardMinX + static_cast<float>(c) * worldCellSize;
                    appendProjectedLine(
                        glm::vec3(x, 0.01f, boardMinZ),
                        glm::vec3(x, 0.01f, boardMaxZ),
                        0.23f, 0.35f, 0.44f, 0.95f, line);
                }
                for (int r = 0; r <= rows; ++r) {
                    const float z = boardMinZ + static_cast<float>(r) * worldCellSize;
                    appendProjectedLine(
                        glm::vec3(boardMinX, 0.01f, z),
                        glm::vec3(boardMaxX, 0.01f, z),
                        0.23f, 0.35f, 0.44f, 0.95f, line);
                }
                if (worldTriangles.size() == boardTrianglesStart2D &&
                    world3DTriangles.size() == boardTrianglesStart3D) {
                    IRenderBackend::DebugQuad boardFallback;
                    boardFallback.x = boardX;
                    boardFallback.y = boardY;
                    boardFallback.w = boardW;
                    boardFallback.h = boardH;
                    boardFallback.r = 0.07f;
                    boardFallback.g = 0.11f;
                    boardFallback.b = 0.15f;
                    boardFallback.a = 0.92f;
                    worldBackgroundQuads.push_back(boardFallback);

                    for (int r = 0; r < rows; ++r) {
                        for (int c = 0; c < cols; ++c) {
                            IRenderBackend::DebugQuad cell;
                            cell.x = boardX + cellW * static_cast<float>(c);
                            cell.y = boardY + cellH * static_cast<float>(r);
                            cell.w = cellW;
                            cell.h = cellH;
                            const bool darkCell = ((r + c) % 2) == 0;
                            cell.r = darkCell ? 0.09f : 0.14f;
                            cell.g = darkCell ? 0.14f : 0.19f;
                            cell.b = darkCell ? 0.19f : 0.25f;
                            cell.a = darkCell ? 0.34f : 0.26f;
                            worldBackgroundQuads.push_back(cell);
                        }
                    }

                    for (int c = 0; c <= cols; ++c) {
                        IRenderBackend::DebugLine vLine;
                        vLine.x1 = boardX + cellW * static_cast<float>(c);
                        vLine.y1 = boardY;
                        vLine.x2 = vLine.x1;
                        vLine.y2 = boardY + boardH;
                        vLine.thickness = line;
                        vLine.r = 0.26f;
                        vLine.g = 0.38f;
                        vLine.b = 0.47f;
                        vLine.a = 0.96f;
                        lines.push_back(vLine);
                    }
                    for (int r = 0; r <= rows; ++r) {
                        IRenderBackend::DebugLine hLine;
                        hLine.x1 = boardX;
                        hLine.y1 = boardY + cellH * static_cast<float>(r);
                        hLine.x2 = boardX + boardW;
                        hLine.y2 = hLine.y1;
                        hLine.thickness = line;
                        hLine.r = 0.26f;
                        hLine.g = 0.38f;
                        hLine.b = 0.47f;
                        hLine.a = 0.96f;
                        lines.push_back(hLine);
                    }
                }

                const auto drawProjectedUnits = [&](const std::vector<PokemonInstance>& units) {
                    for (const auto& unit : units) {
                        if (!unit.alive && !unit.captureInProgress && !unit.fainting) continue;

                        const runtime::backend_anim::ProceduralPose pose =
                            runtime::backend_anim::computeProceduralPose(unit, worldCellSize);
                        const bool activeAttackWindow = pose.activeAttackWindow;
                        const float attackProgress = pose.attackProgress;
                        const glm::vec3 attackOffset =
                            game::runtime::backend_proxy::yawForward(unit.rotation.y) * pose.attackLunge;
                        const float animYaw = pose.yawDeg;
                        const float animPitch = pose.pitchDeg;
                        const float animRoll =
                            pose.rollDeg + (unit.side == PokemonSide::Player ? -pose.faintRoll : pose.faintRoll);
                        const float attackPulse = pose.attackPulse;
                        const glm::vec3 animatedCenter =
                            unit.position + attackOffset +
                            glm::vec3(0.0f, unit.visualYOffset + pose.bobY - pose.faintDrop, 0.0f);
                        const glm::vec3 worldPos =
                            animatedCenter +
                            glm::vec3(0.0f, std::max(0.2f, worldCellSize * 0.22f), 0.0f);
                        float cx = 0.0f;
                        float cy = 0.0f;
                        float cz = 0.0f;
                        if (!projectWorld(worldPos, cx, cy, cz)) continue;
                        if (cz < 0.0f || cz > 1.0f) continue;

                        float sx = 0.0f;
                        float sy = 0.0f;
                        float sz = 0.0f;
                        const bool hasCellX = projectWorld(
                            worldPos + glm::vec3(worldCellSize, 0.0f, 0.0f),
                            sx,
                            sy,
                            sz);
                        float cellPx = hasCellX ? glm::length(glm::vec2(sx - cx, sy - cy)) : 0.0f;
                        if (!std::isfinite(cellPx) || cellPx < 8.0f) {
                            cellPx = std::max(14.0f, minDim * 0.035f);
                        }
                        const float unitSize = std::clamp(cellPx * 0.75f, 10.0f, 84.0f);
                        const game::runtime::backend_proxy::UnitProxyExtents extents =
                            game::runtime::backend_proxy::computeUnitProxyExtents(unit, worldCellSize);
                        const glm::vec3 proxyCenter = animatedCenter;

                        IRenderBackend::DebugQuad tint;
                        runtime::backend_units::applyWorldUnitTint(tint, unit);
                        const float topR = std::clamp(tint.r * 0.86f + 0.12f, 0.0f, 1.0f);
                        const float topG = std::clamp(tint.g * 0.86f + 0.12f, 0.0f, 1.0f);
                        const float topB = std::clamp(tint.b * 0.86f + 0.12f, 0.0f, 1.0f);
                        const float sideR = std::clamp(tint.r * 0.72f, 0.0f, 1.0f);
                        const float sideG = std::clamp(tint.g * 0.72f, 0.0f, 1.0f);
                        const float sideB = std::clamp(tint.b * 0.72f, 0.0f, 1.0f);
                        const float topAlpha = unit.alive ? 0.96f : 0.78f;
                        const float sideAlpha = unit.alive ? 0.88f : 0.70f;
                        const auto shadow = game::runtime::backend_proxy::computeShadowQuad(
                            proxyCenter,
                            extents.halfWidth * 1.15f,
                            extents.halfDepth * 1.15f,
                            animYaw,
                            0.010f);
                        if (supportsWorldTriangles3D) {
                            appendWorldQuad(
                                shadow[0],
                                shadow[1],
                                shadow[2],
                                shadow[3],
                                0.02f,
                                0.03f,
                                0.04f,
                                unit.alive ? 0.42f : 0.24f);
                        } else {
                            appendProjectedQuad(
                                shadow[0],
                                shadow[1],
                                shadow[2],
                                shadow[3],
                                0.02f,
                                0.03f,
                                0.04f,
                                unit.alive ? 0.42f : 0.24f);
                        }

                        bool drewModelMesh = false;
                        if (const runtime::backend_model::MeshData* mesh = resolveModelMesh(unit)) {
                            const std::size_t triangleCount = mesh->indices.size() / 3u;
                            const std::size_t maxTrianglesPerUnit = backendModelTriangleLimit();
                            const std::size_t unitTriangleBudget = std::min(triangleCount, maxTrianglesPerUnit);

                            const float modelScale =
                                std::max(0.01f, mesh->modelScaleFactor) *
                                std::max(0.05f, unit.modelScaleCorrection) *
                                std::max(0.05f, unit.speciesScale) *
                                std::max(0.05f, unit.visualScale) *
                                std::max(0.05f, unit.captureScale) *
                                attackPulse;
                            const glm::vec3 renderPos = proxyCenter;
                            const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(modelScale));
                            const glm::mat4 rotationX =
                                glm::rotate(glm::mat4(1.0f), glm::radians(animPitch), glm::vec3(1, 0, 0));
                            const glm::mat4 rotationY =
                                glm::rotate(glm::mat4(1.0f), glm::radians(animYaw), glm::vec3(0, 1, 0));
                            const glm::mat4 rotationZ =
                                glm::rotate(glm::mat4(1.0f), glm::radians(animRoll), glm::vec3(0, 0, 1));
                            const glm::mat4 translation = glm::translate(glm::mat4(1.0f), renderPos);
                            const glm::mat4 modelM = translation * rotationY * rotationX * rotationZ * scale;
                            const glm::mat3 normalM = glm::transpose(glm::inverse(glm::mat3(modelM)));
                            const std::size_t modelDepthCountBefore = modelDepthTris.size();
                            const std::size_t modelWorld3DCountBefore = world3DTriangles.size();

                            const glm::vec3 lightDir = glm::normalize(glm::vec3(0.45f, 0.90f, 0.35f));
                            const glm::vec3 fallbackBase(
                                std::clamp(tint.r * 0.85f + 0.10f, 0.0f, 1.0f),
                                std::clamp(tint.g * 0.85f + 0.10f, 0.0f, 1.0f),
                                std::clamp(tint.b * 0.85f + 0.10f, 0.0f, 1.0f));

                            const auto pushModelTriangle = [&](const glm::vec3& a,
                                                                const glm::vec3& b,
                                                                const glm::vec3& c,
                                                                const glm::vec3& n0,
                                                                const glm::vec3& n1,
                                                                const glm::vec3& n2,
                                                                const glm::vec3& baseColor0,
                                                                const glm::vec3& baseColor1,
                                                                const glm::vec3& baseColor2,
                                                                float alpha,
                                                                bool doubleSided) {
                                float x1 = 0.0f;
                                float y1 = 0.0f;
                                float z1 = 0.0f;
                                float x2 = 0.0f;
                                float y2 = 0.0f;
                                float z2 = 0.0f;
                                float x3 = 0.0f;
                                float y3 = 0.0f;
                                float z3 = 0.0f;
                                if (!supportsWorldTriangles3D) {
                                    if (!projectWorld(a, x1, y1, z1) ||
                                        !projectWorld(b, x2, y2, z2) ||
                                        !projectWorld(c, x3, y3, z3)) {
                                        return;
                                    }
                                    if ((z1 < 0.0f || z1 > 1.0f) &&
                                        (z2 < 0.0f || z2 > 1.0f) &&
                                        (z3 < 0.0f || z3 > 1.0f)) {
                                        return;
                                    }
                                }

                                glm::vec3 n(0.0f, 1.0f, 0.0f);
                                const glm::vec3 blendedNormal = n0 + n1 + n2;
                                const float blendedLenSq = glm::dot(blendedNormal, blendedNormal);
                                if (blendedLenSq > 0.000001f) {
                                    n = glm::normalize(blendedNormal);
                                } else {
                                    const glm::vec3 rawNormal = glm::cross(b - a, c - a);
                                    const float rawLenSq = glm::dot(rawNormal, rawNormal);
                                    if (rawLenSq > 0.000001f) n = glm::normalize(rawNormal);
                                }
                                const glm::vec3 triCenter = (a + b + c) * (1.0f / 3.0f);
                                glm::vec3 toCamera = cameraWorldPos - triCenter;
                                const float toCameraLenSq = glm::dot(toCamera, toCamera);
                                if (toCameraLenSq > 0.000001f) {
                                    toCamera = glm::normalize(toCamera);
                                } else {
                                    toCamera = glm::vec3(0.0f, 0.0f, -1.0f);
                                }
                                const float lit = std::clamp(glm::dot(n, lightDir), 0.0f, 1.0f);
                                const float facing = std::clamp(glm::dot(n, toCamera), -1.0f, 1.0f);
                                if (!doubleSided && facing <= 0.01f) return;
                                const float rim = std::pow(std::clamp(1.0f - std::max(0.0f, facing), 0.0f, 1.0f), 2.0f);
                                const glm::vec3 halfDir = glm::normalize(lightDir + toCamera);
                                const float spec = std::pow(std::clamp(glm::dot(n, halfDir), 0.0f, 1.0f), 22.0f);
                                const float shade = std::clamp(0.22f + lit * 0.62f + rim * 0.10f + spec * 0.20f, 0.05f, 1.40f);
                                const auto shadeColor = [&](const glm::vec3& cIn) {
                                    const glm::vec3 baseLinear =
                                        glm::pow(glm::clamp(cIn, 0.0f, 1.0f), glm::vec3(2.2f));
                                    const glm::vec3 litLinear = glm::clamp(baseLinear * shade, 0.0f, 1.0f);
                                    return glm::pow(litLinear, glm::vec3(1.0f / 2.2f));
                                };
                                const glm::vec3 shaded0 = shadeColor(baseColor0);
                                const glm::vec3 shaded1 = shadeColor(baseColor1);
                                const glm::vec3 shaded2 = shadeColor(baseColor2);
                                const glm::vec3 shadedAvg = (shaded0 + shaded1 + shaded2) * (1.0f / 3.0f);
                                const float facingFade = 0.60f + 0.40f * std::max(0.0f, facing);
                                const float outAlpha = alpha * facingFade;

                                if (supportsWorldTriangles3D) {
                                    IRenderBackend::WorldTriangle tri3d;
                                    tri3d.x1 = a.x;
                                    tri3d.y1 = a.y;
                                    tri3d.z1 = a.z;
                                    tri3d.x2 = b.x;
                                    tri3d.y2 = b.y;
                                    tri3d.z2 = b.z;
                                    tri3d.x3 = c.x;
                                    tri3d.y3 = c.y;
                                    tri3d.z3 = c.z;
                                    tri3d.r = shadedAvg.r;
                                    tri3d.g = shadedAvg.g;
                                    tri3d.b = shadedAvg.b;
                                    tri3d.a = outAlpha;
                                    tri3d.r1 = shaded0.r;
                                    tri3d.g1 = shaded0.g;
                                    tri3d.b1 = shaded0.b;
                                    tri3d.a1 = outAlpha;
                                    tri3d.r2 = shaded1.r;
                                    tri3d.g2 = shaded1.g;
                                    tri3d.b2 = shaded1.b;
                                    tri3d.a2 = outAlpha;
                                    tri3d.r3 = shaded2.r;
                                    tri3d.g3 = shaded2.g;
                                    tri3d.b3 = shaded2.b;
                                    tri3d.a3 = outAlpha;
                                    world3DTriangles.push_back(tri3d);
                                    return;
                                }

                                DepthTri dt;
                                dt.tri.x1 = x1;
                                dt.tri.y1 = y1;
                                dt.tri.x2 = x2;
                                dt.tri.y2 = y2;
                                dt.tri.x3 = x3;
                                dt.tri.y3 = y3;
                                dt.tri.r = shadedAvg.r;
                                dt.tri.g = shadedAvg.g;
                                dt.tri.b = shadedAvg.b;
                                dt.tri.a = outAlpha;
                                dt.depth = (z1 + z2 + z3) * (1.0f / 3.0f);
                                modelDepthTris.push_back(dt);
                            };
                            const auto safeNormalize = [](const glm::vec3& v) {
                                const float lenSq = glm::dot(v, v);
                                if (lenSq > 1e-12f) return glm::normalize(v);
                                return glm::vec3(0.0f, 1.0f, 0.0f);
                            };

                            std::size_t previousTriSample = triangleCount;
                            for (std::size_t sampleIdx = 0; sampleIdx < unitTriangleBudget; ++sampleIdx) {
                                std::size_t triIdx =
                                    selectUniformTriangleIndex(sampleIdx, unitTriangleBudget, triangleCount);
                                if (unitTriangleBudget < triangleCount && triIdx == previousTriSample) {
                                    if (triIdx + 1u < triangleCount) ++triIdx;
                                }
                                previousTriSample = triIdx;

                                const std::size_t i = triIdx * 3u;
                                const std::uint32_t i0 = mesh->indices[i + 0];
                                const std::uint32_t i1 = mesh->indices[i + 1];
                                const std::uint32_t i2 = mesh->indices[i + 2];
                                if (i0 >= mesh->vertices.size() ||
                                    i1 >= mesh->vertices.size() ||
                                    i2 >= mesh->vertices.size()) {
                                    continue;
                                }

                                const auto& v0 = mesh->vertices[i0];
                                const auto& v1 = mesh->vertices[i1];
                                const auto& v2 = mesh->vertices[i2];

                                const glm::vec3 local0 = runtime::backend_anim::deformLocalVertex(
                                    unit,
                                    pose,
                                    v0.position,
                                    mesh->boundsMin,
                                    mesh->boundsMax,
                                    worldCellSize);
                                const glm::vec3 local1 = runtime::backend_anim::deformLocalVertex(
                                    unit,
                                    pose,
                                    v1.position,
                                    mesh->boundsMin,
                                    mesh->boundsMax,
                                    worldCellSize);
                                const glm::vec3 local2 = runtime::backend_anim::deformLocalVertex(
                                    unit,
                                    pose,
                                    v2.position,
                                    mesh->boundsMin,
                                    mesh->boundsMax,
                                    worldCellSize);

                                const glm::vec3 a = glm::vec3(modelM * glm::vec4(local0, 1.0f));
                                const glm::vec3 b = glm::vec3(modelM * glm::vec4(local1, 1.0f));
                                const glm::vec3 c = glm::vec3(modelM * glm::vec4(local2, 1.0f));
                                const glm::vec3 n0 = safeNormalize(normalM * v0.normal);
                                const glm::vec3 n1 = safeNormalize(normalM * v1.normal);
                                const glm::vec3 n2 = safeNormalize(normalM * v2.normal);

                                auto resolveVertexBase = [&](std::uint32_t vi,
                                                             const runtime::backend_model::MeshVertex& v) {
                                    if (mesh->hasVertexBaseColor && vi < mesh->vertexBaseColors.size()) {
                                        return glm::clamp(mesh->vertexBaseColors[vi], 0.0f, 1.0f);
                                    }
                                    if (mesh->hasVertexColor) {
                                        return glm::clamp(glm::vec3(v.color.r, v.color.g, v.color.b), 0.0f, 1.0f);
                                    }
                                    if (triIdx < mesh->triangleBaseColors.size()) {
                                        return glm::clamp(mesh->triangleBaseColors[triIdx], 0.0f, 1.0f);
                                    }
                                    if (triIdx < mesh->triangleSubmesh.size() &&
                                        !mesh->submeshBaseColors.empty()) {
                                        const std::uint16_t submeshIndex = mesh->triangleSubmesh[triIdx];
                                        if (submeshIndex < mesh->submeshBaseColors.size()) {
                                            const glm::vec4 subColor = mesh->submeshBaseColors[submeshIndex];
                                            return glm::clamp(
                                                glm::vec3(subColor.r, subColor.g, subColor.b), 0.0f, 1.0f);
                                        }
                                    }
                                    (void)vi;
                                    return fallbackBase;
                                };
                                const glm::vec3 baseColor0 = resolveVertexBase(i0, v0);
                                const glm::vec3 baseColor1 = resolveVertexBase(i1, v1);
                                const glm::vec3 baseColor2 = resolveVertexBase(i2, v2);

                                const float triOpacity = (triIdx < mesh->triangleOpacity.size())
                                    ? mesh->triangleOpacity[triIdx]
                                    : 1.0f;
                                const float alpha = (unit.alive ? 1.0f : 0.82f) * std::clamp(triOpacity, 0.0f, 1.0f);
                                if (alpha < 0.03f) continue;
                                const bool triDoubleSided =
                                    (triIdx < mesh->triangleDoubleSided.size()) &&
                                    (mesh->triangleDoubleSided[triIdx] != 0u);
                                pushModelTriangle(
                                    a,
                                    b,
                                    c,
                                    n0,
                                    n1,
                                    n2,
                                    baseColor0,
                                    baseColor1,
                                    baseColor2,
                                    alpha,
                                    triDoubleSided);
                            }

                            drewModelMesh =
                                (modelDepthTris.size() > modelDepthCountBefore) ||
                                (world3DTriangles.size() > modelWorld3DCountBefore);
                        }

                        const game::runtime::backend_proxy::UnitProxyCorners corners =
                            game::runtime::backend_proxy::computeUnitProxyCorners(
                                proxyCenter,
                                extents,
                                animYaw);
                        if (!drewModelMesh) {
                            if (supportsWorldTriangles3D) {
                                appendWorldQuad(
                                    corners.top[0],
                                    corners.top[1],
                                    corners.top[2],
                                    corners.top[3],
                                    topR, topG, topB, topAlpha);
                                appendWorldQuad(
                                    corners.bottom[0], corners.bottom[1], corners.top[1], corners.top[0],
                                    sideR, sideG, sideB, sideAlpha);
                                appendWorldQuad(
                                    corners.bottom[1], corners.bottom[2], corners.top[2], corners.top[1],
                                    sideR, sideG, sideB, sideAlpha);
                                appendWorldQuad(
                                    corners.bottom[2], corners.bottom[3], corners.top[3], corners.top[2],
                                    sideR, sideG, sideB, sideAlpha);
                                appendWorldQuad(
                                    corners.bottom[3], corners.bottom[0], corners.top[0], corners.top[3],
                                    sideR, sideG, sideB, sideAlpha);
                            } else {
                                appendProjectedQuad(
                                    corners.top[0],
                                    corners.top[1],
                                    corners.top[2],
                                    corners.top[3],
                                    topR, topG, topB, topAlpha);
                                appendProjectedQuad(
                                    corners.bottom[0], corners.bottom[1], corners.top[1], corners.top[0],
                                    sideR, sideG, sideB, sideAlpha);
                                appendProjectedQuad(
                                    corners.bottom[1], corners.bottom[2], corners.top[2], corners.top[1],
                                    sideR, sideG, sideB, sideAlpha);
                                appendProjectedQuad(
                                    corners.bottom[2], corners.bottom[3], corners.top[3], corners.top[2],
                                    sideR, sideG, sideB, sideAlpha);
                                appendProjectedQuad(
                                    corners.bottom[3], corners.bottom[0], corners.top[0], corners.top[3],
                                    sideR, sideG, sideB, sideAlpha);
                            }
                        }

                        const glm::vec3 heading = game::runtime::backend_proxy::yawForward(animYaw);
                        const glm::vec3 headingStart =
                            corners.top[0] * 0.25f + corners.top[1] * 0.25f +
                            corners.top[2] * 0.25f + corners.top[3] * 0.25f;
                        appendProjectedLine(
                            headingStart + glm::vec3(0.0f, extents.height * 0.06f, 0.0f),
                            headingStart + heading * std::max(0.08f, extents.halfDepth * 1.1f) +
                                glm::vec3(0.0f, extents.height * 0.06f, 0.0f),
                            0.95f,
                            0.95f,
                            0.98f,
                            unit.alive ? 0.75f : 0.45f,
                            std::max(1.0f, line * 0.9f));
                        if (activeAttackWindow) {
                            const float ringRadius = std::max(0.04f, extents.halfWidth * 1.05f + attackProgress * 0.10f);
                            const int segments = 12;
                            for (int seg = 0; seg < segments; ++seg) {
                                const float t0 = (static_cast<float>(seg) / static_cast<float>(segments)) * 6.2831853f;
                                const float t1 = (static_cast<float>(seg + 1) / static_cast<float>(segments)) * 6.2831853f;
                                const glm::vec3 p0 =
                                    proxyCenter +
                                    glm::vec3(std::cos(t0) * ringRadius, extents.height * 0.25f, std::sin(t0) * ringRadius);
                                const glm::vec3 p1 =
                                    proxyCenter +
                                    glm::vec3(std::cos(t1) * ringRadius, extents.height * 0.25f, std::sin(t1) * ringRadius);
                                appendProjectedLine(
                                    p0,
                                    p1,
                                    0.98f,
                                    0.84f,
                                    0.42f,
                                    0.82f,
                                    std::max(1.0f, line * 0.85f));
                            }
                            if (unit.pendingDamageTargetId >= 0) {
                                if (const PokemonInstance* target = gameWorld->findUnitById(unit.pendingDamageTargetId)) {
                                    const glm::vec3 from = proxyCenter + glm::vec3(0.0f, extents.height * 0.35f, 0.0f);
                                    const glm::vec3 to =
                                        target->position +
                                        glm::vec3(0.0f, std::max(0.12f, worldCellSize * 0.24f) + target->visualYOffset, 0.0f);
                                    appendProjectedLine(
                                        from,
                                        to,
                                        0.98f,
                                        0.62f,
                                        0.28f,
                                        0.74f,
                                        std::max(1.0f, line * 1.0f));
                                }
                            }
                        }

                        const float topLabelY = cy - std::max(14.0f, unitSize * 0.40f);

                        BackendUnitLabel label;
                        label.x = std::max(6.0f, cx - unitSize * 0.55f);
                        label.y = std::max(4.0f, topLabelY);
                        label.text = unit.name;
                        if (!label.text.empty()) {
                            label.text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label.text[0])));
                        }
                        label.color = (unit.side == PokemonSide::Player)
                            ? glm::vec3(0.84f, 0.98f, 0.88f)
                            : glm::vec3(0.98f, 0.84f, 0.80f);
                        unitLabels.push_back(std::move(label));

                        const float hpRatio = std::clamp(
                            static_cast<float>(std::max(0, unit.hp)) /
                                static_cast<float>(std::max(1, unit.maxHP)),
                            0.0f,
                            1.0f);
                        const float hpW = unitSize * 0.86f;
                        const float hpX = cx - hpW * 0.5f;
                        const float hpY = topLabelY - std::max(2.0f, unitSize * 0.10f);
                        const float hpThick = std::max(1.0f, line * 2.1f);

                        IRenderBackend::DebugLine hpBg;
                        hpBg.x1 = hpX;
                        hpBg.y1 = hpY;
                        hpBg.x2 = hpX + hpW;
                        hpBg.y2 = hpY;
                        hpBg.thickness = hpThick;
                        hpBg.r = 0.12f;
                        hpBg.g = 0.12f;
                        hpBg.b = 0.14f;
                        hpBg.a = 0.95f;
                        lines.push_back(hpBg);

                        IRenderBackend::DebugLine hp = hpBg;
                        hp.x1 = hpX + std::max(0.5f, line * 0.4f);
                        hp.x2 = hp.x1 + std::max(0.0f, (hpW - std::max(1.0f, line * 0.8f)) * hpRatio);
                        if (unit.side == PokemonSide::Player) {
                            hp.r = 0.28f;
                            hp.g = 0.92f;
                            hp.b = 0.46f;
                        } else {
                            hp.r = 0.94f;
                            hp.g = 0.38f;
                            hp.b = 0.28f;
                        }
                        hp.a = 1.0f;
                        lines.push_back(hp);

                        if (!unit.chargedMove.empty()) {
                            const float energyRatio = std::clamp(
                                static_cast<float>(std::max(0, unit.energy)) /
                                    static_cast<float>(std::max(1, unit.maxEnergy)),
                                0.0f,
                                1.0f);
                            const float energyY =
                                std::min(static_cast<float>(drawableH) - 2.0f,
                                         cy + unitSize * 0.50f + std::max(2.0f, line * 2.2f));

                            IRenderBackend::DebugLine energyBg;
                            energyBg.x1 = hpX;
                            energyBg.y1 = energyY;
                            energyBg.x2 = hpX + hpW;
                            energyBg.y2 = energyY;
                            energyBg.thickness = std::max(1.0f, line * 1.7f);
                            energyBg.r = 0.10f;
                            energyBg.g = 0.11f;
                            energyBg.b = 0.15f;
                            energyBg.a = 0.95f;
                            lines.push_back(energyBg);

                            IRenderBackend::DebugLine energy = energyBg;
                            energy.x1 = hpX + std::max(0.5f, line * 0.4f);
                            energy.x2 =
                                energy.x1 + std::max(0.0f, (hpW - std::max(1.0f, line * 0.8f)) * energyRatio);
                            energy.r = 0.34f;
                            energy.g = 0.70f;
                            energy.b = 0.98f;
                            energy.a = 1.0f;
                            lines.push_back(energy);
                        }
                    }
                };

                drawProjectedUnits(gameWorld->getPokemons());
                drawProjectedUnits(gameWorld->getBenchPokemons());
                if (!modelDepthTris.empty()) {
                    std::sort(
                        modelDepthTris.begin(),
                        modelDepthTris.end(),
                        [](const DepthTri& lhs, const DepthTri& rhs) {
                            return lhs.depth > rhs.depth;
                        });
                    worldTriangles.reserve(worldTriangles.size() + modelDepthTris.size());
                    for (const DepthTri& tri : modelDepthTris) {
                        worldTriangles.push_back(tri.tri);
                    }
                }
            } else {
            IRenderBackend::DebugQuad boardBg;
            boardBg.x = boardX;
            boardBg.y = boardY;
            boardBg.w = boardW;
            boardBg.h = boardH;
            boardBg.r = renderWorld ? 0.10f : 0.08f;
            boardBg.g = renderWorld ? 0.16f : 0.10f;
            boardBg.b = renderWorld ? 0.20f : 0.14f;
            boardBg.a = renderWorld ? 1.0f : 0.90f;
            worldBackgroundQuads.push_back(boardBg);

            const float line = std::max(1.0f, minDim * 0.002f);
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    IRenderBackend::DebugQuad cell;
                    cell.x = boardX + cellW * static_cast<float>(c);
                    cell.y = boardY + cellH * static_cast<float>(r);
                    cell.w = cellW;
                    cell.h = cellH;
                    const bool darkCell = ((r + c) % 2) == 0;
                    if (darkCell) {
                        cell.r = renderWorld ? 0.08f : 0.07f;
                        cell.g = renderWorld ? 0.13f : 0.09f;
                        cell.b = renderWorld ? 0.18f : 0.12f;
                        cell.a = renderWorld ? 0.28f : 0.22f;
                    } else {
                        cell.r = renderWorld ? 0.16f : 0.10f;
                        cell.g = renderWorld ? 0.22f : 0.14f;
                        cell.b = renderWorld ? 0.27f : 0.18f;
                        cell.a = renderWorld ? 0.18f : 0.14f;
                    }
                    worldBackgroundQuads.push_back(cell);
                }
            }

            for (int c = 0; c <= cols; ++c) {
                IRenderBackend::DebugLine vLine;
                vLine.x1 = boardX + cellW * static_cast<float>(c);
                vLine.y1 = boardY;
                vLine.x2 = vLine.x1;
                vLine.y2 = boardY + boardH;
                vLine.thickness = line;
                vLine.r = renderWorld ? 0.24f : 0.18f;
                vLine.g = renderWorld ? 0.36f : 0.23f;
                vLine.b = renderWorld ? 0.45f : 0.30f;
                vLine.a = 1.0f;
                lines.push_back(vLine);
            }
            for (int r = 0; r <= rows; ++r) {
                IRenderBackend::DebugLine hLine;
                hLine.x1 = boardX;
                hLine.y1 = boardY + cellH * static_cast<float>(r);
                hLine.x2 = boardX + boardW;
                hLine.y2 = hLine.y1;
                hLine.thickness = line;
                hLine.r = renderWorld ? 0.24f : 0.18f;
                hLine.g = renderWorld ? 0.36f : 0.23f;
                hLine.b = renderWorld ? 0.45f : 0.30f;
                hLine.a = 1.0f;
                lines.push_back(hLine);
            }

            if (renderWorld && gameWorld) {
                const float worldCellSize = gameWorld->getBoardCellSize();
                const auto& units = gameWorld->getPokemons();
                for (const auto& unit : units) {
                    if (!unit.alive && !unit.captureInProgress && !unit.fainting) continue;
                    const auto uv = runtime::backendview::worldToBoardUv(
                        unit.position.x,
                        unit.position.z,
                        cols,
                        rows,
                        worldCellSize);
                    if (uv.first < 0.0f || uv.first > 1.0f || uv.second < 0.0f || uv.second > 1.0f) continue;

                    IRenderBackend::DebugQuad u;
                    const float centerX = boardX + uv.first * boardW;
                    const float centerY = boardY + uv.second * boardH;
                    u.w = cellW * 0.60f;
                    u.h = cellH * 0.60f;
                    u.x = centerX - u.w * 0.5f;
                    u.y = centerY - u.h * 0.5f;
                    runtime::backend_units::applyWorldUnitTint(u, unit);
                    worldQuads.push_back(u);

                    const std::string unitImagePath =
                        runtime::backend_units::resolveWorldUnitImagePath(unit.name);
                    IRenderBackend::DebugSprite unitSprite =
                        runtime::backend_units::makeWorldUnitSprite(
                            centerX,
                            centerY,
                            cellW,
                            cellH,
                            unitImagePath,
                            unit.alive ? 0.96f : 0.70f);
                    if (!unitSprite.texturePath.empty()) {
                        sprites.push_back(std::move(unitSprite));
                    }

                    BackendUnitLabel label;
                    label.x = std::max(6.0f, u.x);
                    label.y = std::max(4.0f, u.y - 14.0f);
                    label.text = unit.name;
                    if (!label.text.empty()) {
                        label.text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label.text[0])));
                    }
                    label.color = (unit.side == PokemonSide::Player)
                        ? glm::vec3(0.84f, 0.98f, 0.88f)
                        : glm::vec3(0.98f, 0.84f, 0.80f);
                    unitLabels.push_back(std::move(label));

                    const float hpRatio = std::clamp(
                        static_cast<float>(std::max(0, unit.hp)) /
                            static_cast<float>(std::max(1, unit.maxHP)),
                        0.0f,
                        1.0f);
                    const float hpY = std::max(2.0f, u.y - std::max(2.0f, line * 2.0f));
                    IRenderBackend::DebugLine hpBg;
                    hpBg.x1 = u.x;
                    hpBg.y1 = hpY;
                    hpBg.x2 = u.x + u.w;
                    hpBg.y2 = hpY;
                    hpBg.thickness = std::max(2.0f, line * 2.2f);
                    hpBg.r = 0.12f;
                    hpBg.g = 0.12f;
                    hpBg.b = 0.14f;
                    hpBg.a = 0.95f;
                    lines.push_back(hpBg);

                    IRenderBackend::DebugLine hp;
                    hp.x1 = u.x + std::max(0.5f, line * 0.4f);
                    hp.y1 = hpY;
                    hp.x2 = hp.x1 + std::max(0.0f, (u.w - std::max(1.0f, line * 0.8f)) * hpRatio);
                    hp.y2 = hpY;
                    hp.thickness = std::max(1.0f, line * 1.4f);
                    if (unit.side == PokemonSide::Player) {
                        hp.r = 0.28f;
                        hp.g = 0.92f;
                        hp.b = 0.46f;
                    } else {
                        hp.r = 0.94f;
                        hp.g = 0.38f;
                        hp.b = 0.28f;
                    }
                    hp.a = 1.0f;
                    lines.push_back(hp);

                    if (!unit.chargedMove.empty()) {
                        const float energyRatio = std::clamp(
                            static_cast<float>(std::max(0, unit.energy)) /
                                static_cast<float>(std::max(1, unit.maxEnergy)),
                            0.0f,
                            1.0f);
                        const float energyY = std::min(
                            boardY + boardH - 2.0f,
                            u.y + u.h + std::max(2.0f, line * 2.4f));

                        IRenderBackend::DebugLine energyBg;
                        energyBg.x1 = u.x;
                        energyBg.y1 = energyY;
                        energyBg.x2 = u.x + u.w;
                        energyBg.y2 = energyY;
                        energyBg.thickness = std::max(1.0f, line * 1.8f);
                        energyBg.r = 0.10f;
                        energyBg.g = 0.11f;
                        energyBg.b = 0.15f;
                        energyBg.a = 0.95f;
                        lines.push_back(energyBg);

                        IRenderBackend::DebugLine energy = energyBg;
                        energy.x1 = u.x + std::max(0.5f, line * 0.4f);
                        energy.x2 =
                            energy.x1 + std::max(0.0f, (u.w - std::max(1.0f, line * 0.8f)) * energyRatio);
                        energy.r = 0.34f;
                        energy.g = 0.70f;
                        energy.b = 0.98f;
                        energy.a = 1.0f;
                        lines.push_back(energy);
                    }
                }

                const auto& benchUnits = gameWorld->getBenchPokemons();
                if (!benchUnits.empty()) {
                    const int benchSlots = std::max(1, config.benchSlots);
                    const float benchGap = std::max(12.0f, minDim * 0.02f);
                    const float benchH = std::max(26.0f, minDim * 0.085f);
                    const float benchW = std::max(160.0f, std::min(boardW, static_cast<float>(drawableW) - 40.0f));
                    const float benchX = (static_cast<float>(drawableW) - benchW) * 0.5f;
                    const float desiredBenchY = boardY + boardH + benchGap;
                    const float benchY = std::min(desiredBenchY, static_cast<float>(drawableH) - benchH - 24.0f);
                    if (benchY > boardY + boardH + 3.0f) {
                        IRenderBackend::DebugQuad benchBg;
                        benchBg.x = benchX;
                        benchBg.y = benchY;
                        benchBg.w = benchW;
                        benchBg.h = benchH;
                        benchBg.r = 0.09f;
                        benchBg.g = 0.12f;
                        benchBg.b = 0.15f;
                        benchBg.a = 0.96f;
                        worldQuads.push_back(benchBg);

                        const float benchCellW = benchW / static_cast<float>(benchSlots);
                        const float benchLineThickness = std::max(1.0f, line * 0.95f);
                        for (int slot = 0; slot <= benchSlots; ++slot) {
                            IRenderBackend::DebugLine slotLine;
                            slotLine.x1 = benchX + benchCellW * static_cast<float>(slot);
                            slotLine.y1 = benchY;
                            slotLine.x2 = slotLine.x1;
                            slotLine.y2 = benchY + benchH;
                            slotLine.thickness = benchLineThickness;
                            slotLine.r = 0.20f;
                            slotLine.g = 0.26f;
                            slotLine.b = 0.32f;
                            slotLine.a = 1.0f;
                            lines.push_back(slotLine);
                        }

                        IRenderBackend::DebugLine top;
                        top.x1 = benchX;
                        top.y1 = benchY;
                        top.x2 = benchX + benchW;
                        top.y2 = benchY;
                        top.thickness = benchLineThickness;
                        top.r = 0.24f;
                        top.g = 0.30f;
                        top.b = 0.36f;
                        top.a = 1.0f;
                        lines.push_back(top);

                        IRenderBackend::DebugLine bottom = top;
                        bottom.y1 = benchY + benchH;
                        bottom.y2 = benchY + benchH;
                        lines.push_back(bottom);

                        for (const auto& unit : benchUnits) {
                            const int slot = runtime::backendview::worldToBenchSlot(
                                unit.position.x,
                                benchSlots,
                                worldCellSize);
                            IRenderBackend::DebugQuad benchUnit;
                            benchUnit.x = benchX + benchCellW * static_cast<float>(slot) + benchCellW * 0.20f;
                            benchUnit.y = benchY + benchH * 0.20f;
                            benchUnit.w = benchCellW * 0.60f;
                            benchUnit.h = benchH * 0.60f;
                            benchUnit.r = 0.34f;
                            benchUnit.g = 0.73f;
                            benchUnit.b = 0.96f;
                            benchUnit.a = 0.24f;
                            worldQuads.push_back(benchUnit);

                            const std::string benchImagePath =
                                runtime::backend_units::resolveWorldUnitImagePath(unit.name);
                            IRenderBackend::DebugSprite benchSprite =
                                runtime::backend_units::makeBenchUnitSprite(
                                    benchUnit.x,
                                    benchUnit.y,
                                    benchUnit.w,
                                    benchUnit.h,
                                    benchImagePath,
                                    0.92f);
                            if (!benchSprite.texturePath.empty()) {
                                sprites.push_back(std::move(benchSprite));
                            }

                        }
                    }
                }
            }
            }
        }

        if (showPerfOverlay && engineServices) {
            const EngineFramePerfStats& perf = engineServices->framePerf;
            if (perf.fps > 0.0f) {
                const float fpsNorm = std::clamp(perf.fps / 120.0f, 0.0f, 1.0f);
                IRenderBackend::DebugQuad fpsBarBg;
                fpsBarBg.x = edgePad;
                fpsBarBg.y = std::max(8.0f, edgePad - lineStep * 0.2f);
                fpsBarBg.w = std::clamp(220.0f * uiScale, 140.0f, 320.0f);
                fpsBarBg.h = std::clamp(10.0f * uiScale, 8.0f, 16.0f);
                fpsBarBg.r = 0.15f;
                fpsBarBg.g = 0.15f;
                fpsBarBg.b = 0.18f;
                fpsBarBg.a = 1.0f;
                overlayQuads.push_back(fpsBarBg);

                IRenderBackend::DebugQuad fpsBar = fpsBarBg;
                fpsBar.w *= fpsNorm;
                fpsBar.r = (fpsNorm < 0.5f) ? 0.85f : 0.30f;
                fpsBar.g = (fpsNorm < 0.5f) ? 0.28f : 0.88f;
                fpsBar.b = 0.30f;
                overlayQuads.push_back(fpsBar);
            }
        }

        const auto appendText = [&](float x,
                                    float y,
                                    const std::string& text,
                                    float scale,
                                    const glm::vec3& color) {
            runtime::backend_text::appendTextQuads(
                overlayQuads, x, y, text, scale, color.r, color.g, color.b, 1.0f);
        };
        const auto appendRightText = [&](float y,
                                         const std::string& text,
                                         float scale,
                                         const glm::vec3& color) {
            const float textW = std::max(1.0f, runtime::backend_text::measureTextWidth(text, scale));
            const float x = std::max(edgePad, static_cast<float>(drawableW) - textW - edgePad);
            appendText(x, y, text, scale, color);
        };

        const std::string mode = (services ? services->gameMode : std::string("classic"));
        appendText(edgePad,
                   edgePad + lineStep * 1.1f,
                   runtime::backend_status_text::modeLine(mode),
                   std::clamp(1.2f * uiScale, 0.95f, 1.7f),
                   glm::vec3(0.93f, 0.95f, 0.99f));
        if (services) {
            appendText(edgePad,
                       edgePad + lineStep * 2.2f,
                       runtime::backend_status_text::backendLine(
                           services->activeRendererBackend,
                           services->gpuRenderer),
                       std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                       glm::vec3(0.68f, 0.80f, 0.94f));
        }

        RoundPhase roundPhase = RoundPhase::Planning;
        bool combatActive = false;
        if (ecsWorld.alive(roundPhaseEntity)) {
            if (const auto* roundState = ecsWorld.get<game::RoundState>(roundPhaseEntity)) {
                roundPhase = roundState->phase;
            }
            if (const auto* combatState = ecsWorld.get<game::CombatActive>(roundPhaseEntity)) {
                combatActive = combatState->active;
            }
        }
        appendText(edgePad,
                   edgePad + lineStep * 3.3f,
                       runtime::backend_status_text::roundLine(roundPhase, combatActive),
                       std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                       glm::vec3(0.83f, 0.91f, 0.98f));

        if (!unitLabels.empty()) {
            for (const auto& label : unitLabels) {
                appendText(label.x, label.y, label.text, 0.80f, label.color);
            }
        }

        int playerAlive = 0;
        int enemyAlive = 0;
        if (gameWorld) {
            for (const auto& unit : gameWorld->getPokemons()) {
                if (!unit.alive && !unit.captureInProgress) continue;
                if (unit.side == PokemonSide::Player) ++playerAlive;
                else ++enemyAlive;
            }
            appendText(edgePad,
                       edgePad + lineStep * 4.4f,
                       runtime::backend_status_text::unitsLine(playerAlive, enemyAlive),
                       std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                       glm::vec3(0.72f, 0.90f, 0.84f));
            appendText(edgePad,
                       edgePad + lineStep * 5.5f,
                       runtime::backend_status_text::goldLine(gameWorld->getMoney()),
                       std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                       glm::vec3(0.96f, 0.88f, 0.56f));
            const std::string selectedItem = gameWorld->getSelectedItem();
            if (!selectedItem.empty()) {
                appendText(edgePad,
                           edgePad + lineStep * 6.6f,
                           runtime::backend_status_text::selectedItemLine(selectedItem),
                           std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                           glm::vec3(0.84f, 0.90f, 0.98f));
            }

            refreshBackendInventoryFromWorld();
            const auto& inventoryModel = backendInventoryPanel.model;
            const float leftX = edgePad;
            const float invStartY = edgePad + lineStep * 7.7f;

            if (inventoryModel.totalCount > 0 || !selectedItem.empty()) {
                float invY = invStartY;
                appendText(leftX,
                           invY,
                           runtime::backend_inventory::makeTitleLabel(inventoryModel),
                           std::clamp(1.0f * uiScale, 0.80f, 1.30f),
                           glm::vec3(0.92f, 0.95f, 0.99f));
                invY += lineStep;

                const bool hasPrev = runtime::backend_inventory::canScrollPrev(inventoryModel);
                const bool hasNext = runtime::backend_inventory::canScrollNext(inventoryModel);
                if (hasPrev || hasNext) {
                    constexpr float kNavScale = 0.84f;
                    const std::string prevLabel = runtime::backend_inventory::prevPageLabel();
                    const std::string nextLabel = runtime::backend_inventory::nextPageLabel();
                    appendText(leftX,
                               invY,
                               prevLabel,
                               kNavScale,
                               hasPrev ? glm::vec3(0.75f, 0.87f, 0.96f)
                                       : glm::vec3(0.42f, 0.48f, 0.55f));
                    if (hasPrev) {
                        runtime::backend_inventory_panel::HitRegion prevHit;
                        prevHit.action = runtime::backend_inventory_panel::HitAction::ScrollOffset;
                        prevHit.offsetDelta = -1;
                        prevHit.x = leftX;
                        prevHit.y = invY;
                        prevHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(prevLabel, kNavScale));
                        prevHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(prevLabel, kNavScale));
                        backendInventoryPanel.hitRegions.push_back(std::move(prevHit));
                    }

                    const float nextX = leftX + std::max(1.0f, runtime::backend_text::measureTextWidth(prevLabel, kNavScale)) + std::max(10.0f, lineStep * 0.75f);
                    appendText(nextX,
                               invY,
                               nextLabel,
                               kNavScale,
                               hasNext ? glm::vec3(0.75f, 0.87f, 0.96f)
                                       : glm::vec3(0.42f, 0.48f, 0.55f));
                    if (hasNext) {
                        runtime::backend_inventory_panel::HitRegion nextHit;
                        nextHit.action = runtime::backend_inventory_panel::HitAction::ScrollOffset;
                        nextHit.offsetDelta = 1;
                        nextHit.x = nextX;
                        nextHit.y = invY;
                        nextHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(nextLabel, kNavScale));
                        nextHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(nextLabel, kNavScale));
                        backendInventoryPanel.hitRegions.push_back(std::move(nextHit));
                    }
                    invY += lineStep * 0.88f;
                }

                for (const auto& row : inventoryModel.rows) {
                    constexpr float kItemScale = 0.95f;
                    appendText(leftX,
                               invY,
                               row.line,
                               kItemScale,
                               row.selected
                                   ? glm::vec3(0.98f, 0.90f, 0.58f)
                                   : glm::vec3(0.84f, 0.90f, 0.97f));
                    runtime::backend_inventory_panel::HitRegion hit;
                    hit.action = runtime::backend_inventory_panel::HitAction::SelectItem;
                    hit.itemId = row.itemId;
                    hit.x = leftX;
                    hit.y = invY;
                    hit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(row.line, kItemScale));
                    hit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(row.line, kItemScale));
                    backendInventoryPanel.hitRegions.push_back(std::move(hit));
                    invY += lineStep * 0.93f;
                }

                const std::string clearLine = runtime::backend_inventory::clearSelectionLabel();
                appendText(leftX,
                           invY + 1.0f,
                           clearLine,
                           0.90f,
                           selectedItem.empty()
                               ? glm::vec3(0.62f, 0.68f, 0.76f)
                               : glm::vec3(0.95f, 0.78f, 0.66f));
                runtime::backend_inventory_panel::HitRegion clearHit;
                clearHit.action = runtime::backend_inventory_panel::HitAction::ClearSelection;
                clearHit.itemId.clear();
                clearHit.x = leftX;
                clearHit.y = invY + 1.0f;
                clearHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(clearLine, 0.90f));
                clearHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(clearLine, 0.90f));
                backendInventoryPanel.hitRegions.push_back(std::move(clearHit));
                invY += lineStep;
                appendText(leftX,
                           invY + 2.0f,
                           runtime::backend_inventory::hintLabel(),
                           0.82f,
                           glm::vec3(0.66f, 0.76f, 0.90f));
            }

            auto typeCounts = gameWorld->getPlayerTypeLineCounts();
            if (!typeCounts.empty()) {
                std::sort(typeCounts.begin(), typeCounts.end(),
                          [](const GameWorld::TypeLineCount& a, const GameWorld::TypeLineCount& b) {
                              if (a.uniqueLineCount != b.uniqueLineCount) {
                                  return a.uniqueLineCount > b.uniqueLineCount;
                              }
                              return a.type < b.type;
                          });

                float typeY = edgePad + lineStep * 6.6f;
                appendRightText(typeY, "Type Lines", std::clamp(1.0f * uiScale, 0.80f, 1.30f), glm::vec3(0.98f, 0.90f, 0.60f));
                typeY += lineStep;
                const std::size_t maxRows = std::min<std::size_t>(6, typeCounts.size());
                for (std::size_t i = 0; i < maxRows; ++i) {
                    appendRightText(typeY,
                                    runtime::hud::formatTypeLineEntry(typeCounts[i].type, typeCounts[i].uniqueLineCount),
                                    0.95f,
                                    glm::vec3(0.92f, 0.94f, 0.98f));
                    typeY += lineStep * 0.93f;
                }
            }

            const auto& benchUnits = gameWorld->getBenchPokemons();
            if (!benchUnits.empty()) {
                float benchY = edgePad + lineStep * 13.6f;
                appendText(edgePad, benchY, "Bench", std::clamp(1.0f * uiScale, 0.80f, 1.30f), glm::vec3(0.86f, 0.94f, 0.98f));
                benchY += lineStep;
                const std::size_t maxRows = std::min<std::size_t>(5, benchUnits.size());
                for (std::size_t i = 0; i < maxRows; ++i) {
                    appendText(edgePad,
                               benchY,
                               runtime::hud::formatUnitEntry(benchUnits[i].name, benchUnits[i].level),
                               0.95f,
                               glm::vec3(0.80f, 0.88f, 0.96f));
                    benchY += lineStep * 0.93f;
                }
            }

            const auto& shopCards = gameWorld->getClassicShopCards();
            if (!shopCards.empty()) {
                float shopY = edgePad + lineStep * 13.6f;
                appendRightText(shopY, "Shop Offers", std::clamp(1.0f * uiScale, 0.80f, 1.30f), glm::vec3(0.98f, 0.90f, 0.60f));
                shopY += lineStep;
                const std::size_t maxRows = std::min<std::size_t>(5, shopCards.size());
                for (std::size_t i = 0; i < maxRows; ++i) {
                    appendRightText(shopY,
                                    runtime::hud::formatShopCardEntry(shopCards[i].name,
                                                                      shopCards[i].level,
                                                                      shopCards[i].cost),
                                    0.95f,
                                    glm::vec3(0.92f, 0.94f, 0.98f));
                    shopY += lineStep * 0.93f;
                }
            }
        }

        const auto recentMain = log.recentMainLines(7);
        if (!recentMain.empty()) {
            float y = std::max(edgePad + lineStep * 7.0f, static_cast<float>(drawableH) - lineStep * 11.0f);
            for (const auto& line : recentMain) {
                appendText(edgePad,
                           y,
                           trimDebugLine(line.text, 84),
                           1.0f,
                           glm::vec3(
                               std::clamp(line.color.r, 0.0f, 1.0f),
                               std::clamp(line.color.g, 0.0f, 1.0f),
                               std::clamp(line.color.b, 0.0f, 1.0f)));
                y += lineStep;
            }
        }

        const bool classicMode = (mode == "classic");
        const auto sideLines = classicMode ? log.recentEconomyLines(5) : log.recentCatchLines(5);
        if (!sideLines.empty()) {
            float y = std::max(edgePad + lineStep * 7.0f, static_cast<float>(drawableH) - lineStep * 11.0f);
            for (const auto& line : sideLines) {
                const std::string text = trimDebugLine(line.text, 54);
                const float scale = 1.0f;
                const float textW = std::max(1.0f, runtime::backend_text::measureTextWidth(text, scale));
                const float x = std::max(edgePad, static_cast<float>(drawableW) - textW - edgePad);
                appendText(x,
                           y,
                           text,
                           scale,
                           glm::vec3(
                               std::clamp(line.color.r, 0.0f, 1.0f),
                               std::clamp(line.color.g, 0.0f, 1.0f),
                               std::clamp(line.color.b, 0.0f, 1.0f)));
                y += lineStep;
            }
        }

        if (!worldBackgroundQuads.empty()) {
            renderer->drawDebugQuads(worldBackgroundQuads.data(), worldBackgroundQuads.size(), drawableW, drawableH);
        }
        if (!world3DTriangles.empty() && hasWorldViewProj && supportsWorldTriangles3D) {
            renderer->drawWorldTriangles(
                world3DTriangles.data(),
                world3DTriangles.size(),
                worldViewProj,
                drawableW,
                drawableH);
        }
        if (!worldTriangles.empty()) {
            renderer->drawDebugTriangles(worldTriangles.data(), worldTriangles.size(), drawableW, drawableH);
        }
        if (!worldQuads.empty()) {
            renderer->drawDebugQuads(worldQuads.data(), worldQuads.size(), drawableW, drawableH);
        }
        if (!sprites.empty()) {
            renderer->drawDebugSprites(sprites.data(), sprites.size(), drawableW, drawableH);
        }
        if (!lines.empty()) {
            renderer->drawDebugLines(lines.data(), lines.size(), drawableW, drawableH);
        }
        if (!overlayQuads.empty()) {
            renderer->drawDebugQuads(overlayQuads.data(), overlayQuads.size(), drawableW, drawableH);
        }
    }

    void renderLegacyWorldLayer(int drawableW, int drawableH, bool renderWorld) {
        if (!renderWorld) return;
        if (board && gameWorld) {
            board->setCellSize(gameWorld->getBoardCellSize());
        }
        if (gameWorld && camera && board) gameWorld->drawAll(*camera, *board);
        if (gameWorld && camera) {
            auto healthBarData = gameWorld->getHealthBarData(*camera, drawableW, drawableH);
            healthBarRenderer.render(healthBarData);
        }
    }

    void renderLegacyHudLayer(int drawableW, int drawableH, bool renderWorld) {
        if (!renderWorld) return;

        if (gameWorld) {
            const bool adventureMode = services && services->gameMode == "adventure";
            const int invTopInset = adventureMode
                ? std::max(110, static_cast<int>(std::round(static_cast<float>(drawableH) * 0.16f)))
                : 16;
            const int invRightInset = adventureMode ? 24 : 16;
            itemInventoryUI.setLayoutInsets(invTopInset, invRightInset, 16, 16);
            itemInventoryUI.updateFromWorld(*gameWorld, drawableW, drawableH);
        }
        itemInventoryUI.render(drawableW, drawableH);

        const float cornerX = std::round(std::max(10.0f, static_cast<float>(drawableW) * 0.012f));
        const float cornerY = std::round(std::max(10.0f, static_cast<float>(drawableH) * 0.020f));
        const float minDim = static_cast<float>(std::min(drawableW, drawableH));
        const bool classicMode = services && services->gameMode == "classic";
        const auto computeClassicShopTopY = [&]() -> float {
            const game::ui::ShopRowLayout layout = game::ui::computeShopRowLayout(drawableW, drawableH, false);
            const game::ui::ShopRowPlacement place =
                game::ui::computeShopRowPlacement(drawableW, drawableH, 0, layout);
            return static_cast<float>(place.y);
        };
        const float classicShopTopY = computeClassicShopTopY();

        if (battleFeed) {
            const float wrap = std::max(240.0f, std::min(640.0f, static_cast<float>(drawableW) * 0.42f));
            battleFeed->setWrapWidth(wrap);
            battleFeed->setBaseScale(0.40f);
            const float battleLift = std::max(18.0f, minDim * 0.03f);
            battleFeed->setPadding(cornerX, cornerY + battleLift);
            battleFeed->clearBaselineYOverride();
        }
        if (catchFeed) {
            const float wrap = std::max(200.0f, std::min(420.0f, static_cast<float>(drawableW) * 0.30f));
            catchFeed->setWrapWidth(wrap);
            catchFeed->setBaseScale(0.38f);
            catchFeed->setPadding(cornerX, cornerY);
            catchFeed->clearBaselineYOverride();
        }
        if (economyFeed) {
            const float wrap = std::max(220.0f, std::min(380.0f, static_cast<float>(drawableW) * 0.28f));
            economyFeed->setWrapWidth(wrap);
            economyFeed->setBaseScale(0.36f);
            economyFeed->setPadding(cornerX, cornerY);
            economyFeed->clearBaselineYOverride();
            if (classicMode) {
                const float clearance = std::max(22.0f, minDim * 0.03f);
                economyFeed->setBaselineYOverride(std::max(8.0f, classicShopTopY - clearance));
            }
        }
        if (battleFeed) battleFeed->render(drawableW, drawableH);
        if (classicMode) {
            if (economyFeed) economyFeed->render(drawableW, drawableH);
        } else {
            if (catchFeed) catchFeed->render(drawableW, drawableH);
        }

        if (gameWorld && typeBonusText) {
            const auto counts = gameWorld->getPlayerTypeLineCounts();
            if (!counts.empty()) {
                auto formatType = [](std::string t) {
                    if (t.empty()) return t;
                    t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])));
                    return t;
                };

                const float panelX = std::round(std::max(10.0f, static_cast<float>(drawableW) * 0.012f));
                const float panelY = std::round(std::max(110.0f, static_cast<float>(drawableH) * 0.20f));
                const float titleScale = 0.44f;
                const float rowScale = 0.40f;

                typeBonusText->renderText("Type Lines", panelX, panelY,
                                          glm::vec3(0.98f, 0.90f, 0.60f), titleScale);

                float y = panelY + typeBonusText->measureTextHeight(titleScale) + 6.0f;
                for (const auto& entry : counts) {
                    const std::string line = formatType(entry.type) + " x" + std::to_string(entry.uniqueLineCount);
                    typeBonusText->renderText(line, panelX, y, glm::vec3(0.94f, 0.94f, 0.94f), rowScale);
                    y += typeBonusText->measureTextHeight(rowScale) + 3.0f;
                }
            }
        }

        if (showPerfOverlay && typeBonusText && engineServices) {
            const EngineFramePerfStats& perf = engineServices->framePerf;
            if (perf.fps > 0.0f) {
                std::ostringstream line1;
                line1 << std::fixed << std::setprecision(1)
                      << "FPS " << perf.fps
                      << "  frame " << perf.frameMs << "ms"
                      << "  fixed " << perf.fixedMs << "ms"
                      << "  render " << perf.renderMs << "ms"
                      << "  swap " << perf.swapMs << "ms";

                const std::string stats = line1.str();
                const std::string ticks = "ticks " + std::to_string(perf.fixedTicks);

                const float scale = 0.34f;
                const float xPad = std::round(std::max(10.0f, static_cast<float>(drawableW) * 0.012f));
                const float yPad = std::round(std::max(10.0f, static_cast<float>(drawableH) * 0.020f));

                const float statsW = typeBonusText->measureTextWidth(stats, scale);
                const float statsX = std::max(8.0f, static_cast<float>(drawableW) - statsW - xPad);
                typeBonusText->renderText(stats, statsX, yPad, glm::vec3(0.96f, 0.96f, 0.65f), scale);

                const float ticksW = typeBonusText->measureTextWidth(ticks, scale);
                const float ticksX = std::max(8.0f, static_cast<float>(drawableW) - ticksW - xPad);
                const float ticksY = yPad + typeBonusText->measureTextHeight(scale) + 2.0f;
                typeBonusText->renderText(ticks, ticksX, ticksY, glm::vec3(0.86f, 0.93f, 0.98f), scale);
            }
        }
    }

    void renderStateLayer() {
        if (stateManager) {
            stateManager->render();
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

        const auto flow = runtime::render::decideFrameRenderFlow(
            renderEnabled, legacyRenderPath, renderWorld);

        if (flow.renderLegacyWorldLayer) {
            renderLegacyWorldLayer(drawableW, drawableH, renderWorld);
        }
        if (flow.renderBackendDebugLayer) {
            renderBackendDebugView(drawableW, drawableH, renderWorld);
        }
        if (flow.renderStateLayer) {
            renderStateLayer();
        }
        if (flow.renderLegacyHudLayer) {
            renderLegacyHudLayer(drawableW, drawableH, renderWorld);
        }
    }

    void shutdown() {
        std::cout << "[Shutdown] Game.\n";

        log.attach(nullptr);
        log.attachCatchFeed(nullptr);
        log.attachEconomyFeed(nullptr);

        if (board) {
            board->shutdown();
            board.reset();
        }

        if (legacyRenderPath) {
            UIManager::shutdown();
        }

        battleFeed.reset();
        catchFeed.reset();
        economyFeed.reset();
        shopSystem = nullptr;
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

