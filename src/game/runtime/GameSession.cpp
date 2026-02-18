#include "game/runtime/GameSession.h"

// Heavy includes live here (not in headers).
#include <iostream>
#include <string>
#include <utility>
#include <cctype>
#include <random>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

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
#include "game/runtime/BackendHudFormatting.h"
#include "game/runtime/BackendWorldProjection.h"
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

        std::vector<IRenderBackend::DebugQuad> quads;
        quads.reserve(1024);
        std::vector<IRenderBackend::DebugLine> lines;
        lines.reserve(512);
        struct BackendUnitLabel {
            float x = 0.0f;
            float y = 0.0f;
            std::string text;
            glm::vec3 color{1.0f, 1.0f, 1.0f};
        };
        std::vector<BackendUnitLabel> unitLabels;
        unitLabels.reserve(64);

        const int rows = std::max(1, config.rows);
        const int cols = std::max(1, config.cols);
        const float minDim = static_cast<float>(std::min(drawableW, drawableH));
        const float boardW = std::max(240.0f, minDim * 0.78f);
        const float boardH = std::max(180.0f, minDim * 0.58f);
        const float boardX = (static_cast<float>(drawableW) - boardW) * 0.5f;
        const float boardY = (static_cast<float>(drawableH) - boardH) * 0.5f;
        const float cellW = boardW / static_cast<float>(cols);
        const float cellH = boardH / static_cast<float>(rows);

        const bool showWorldBackdrop = renderWorld || (services && services->activeRendererBackend != "opengl");
        if (showWorldBackdrop) {
            IRenderBackend::DebugQuad boardBg;
            boardBg.x = boardX;
            boardBg.y = boardY;
            boardBg.w = boardW;
            boardBg.h = boardH;
            boardBg.r = renderWorld ? 0.10f : 0.08f;
            boardBg.g = renderWorld ? 0.16f : 0.10f;
            boardBg.b = renderWorld ? 0.20f : 0.14f;
            boardBg.a = renderWorld ? 1.0f : 0.90f;
            quads.push_back(boardBg);

            const float line = std::max(1.0f, minDim * 0.002f);
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

                    if (unit.side == PokemonSide::Player) {
                        u.r = 0.16f;
                        u.g = 0.84f;
                        u.b = 0.40f;
                    } else {
                        u.r = 0.90f;
                        u.g = 0.28f;
                        u.b = 0.22f;
                    }
                    if (!unit.alive && unit.captureInProgress) {
                        u.r = 0.98f;
                        u.g = 0.82f;
                        u.b = 0.30f;
                    } else if (!unit.alive) {
                        u.r *= 0.45f;
                        u.g *= 0.45f;
                        u.b *= 0.45f;
                    }
                    u.a = 0.26f;
                    quads.push_back(u);

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
                        quads.push_back(benchBg);

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
                            quads.push_back(benchUnit);

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
                fpsBarBg.x = 20.0f;
                fpsBarBg.y = 20.0f;
                fpsBarBg.w = 220.0f;
                fpsBarBg.h = 10.0f;
                fpsBarBg.r = 0.15f;
                fpsBarBg.g = 0.15f;
                fpsBarBg.b = 0.18f;
                fpsBarBg.a = 1.0f;
                quads.push_back(fpsBarBg);

                IRenderBackend::DebugQuad fpsBar = fpsBarBg;
                fpsBar.w *= fpsNorm;
                fpsBar.r = (fpsNorm < 0.5f) ? 0.85f : 0.30f;
                fpsBar.g = (fpsNorm < 0.5f) ? 0.28f : 0.88f;
                fpsBar.b = 0.30f;
                quads.push_back(fpsBar);
            }
        }

        const auto appendText = [&](float x,
                                    float y,
                                    const std::string& text,
                                    float scale,
                                    const glm::vec3& color) {
            runtime::backend_text::appendTextQuads(
                quads, x, y, text, scale, color.r, color.g, color.b, 1.0f);
        };
        const auto appendRightText = [&](float y,
                                         const std::string& text,
                                         float scale,
                                         const glm::vec3& color) {
            const float textW = std::max(1.0f, runtime::backend_text::measureTextWidth(text, scale));
            const float x = std::max(20.0f, static_cast<float>(drawableW) - textW - 22.0f);
            appendText(x, y, text, scale, color);
        };

        const std::string mode = (services ? services->gameMode : std::string("classic"));
        appendText(20.0f,
                   38.0f,
                   runtime::backend_status_text::modeLine(mode),
                   1.2f,
                   glm::vec3(0.93f, 0.95f, 0.99f));
        if (services) {
            appendText(20.0f,
                       56.0f,
                       runtime::backend_status_text::backendLine(
                           services->activeRendererBackend,
                           services->gpuRenderer),
                       1.0f,
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
        appendText(20.0f,
                   74.0f,
                       runtime::backend_status_text::roundLine(roundPhase, combatActive),
                       1.0f,
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
            appendText(20.0f,
                       92.0f,
                       runtime::backend_status_text::unitsLine(playerAlive, enemyAlive),
                       1.0f,
                       glm::vec3(0.72f, 0.90f, 0.84f));
            appendText(20.0f,
                       110.0f,
                       runtime::backend_status_text::goldLine(gameWorld->getMoney()),
                       1.0f,
                       glm::vec3(0.96f, 0.88f, 0.56f));
            const std::string selectedItem = gameWorld->getSelectedItem();
            if (!selectedItem.empty()) {
                appendText(20.0f,
                           128.0f,
                           runtime::backend_status_text::selectedItemLine(selectedItem),
                           1.0f,
                           glm::vec3(0.84f, 0.90f, 0.98f));
            }

            refreshBackendInventoryFromWorld();
            const auto& inventoryModel = backendInventoryPanel.model;

            if (inventoryModel.totalCount > 0 || !selectedItem.empty()) {
                float invY = 146.0f;
                appendText(20.0f,
                           invY,
                           runtime::backend_inventory::makeTitleLabel(inventoryModel),
                           1.0f,
                           glm::vec3(0.92f, 0.95f, 0.99f));
                invY += 16.0f;

                const bool hasPrev = runtime::backend_inventory::canScrollPrev(inventoryModel);
                const bool hasNext = runtime::backend_inventory::canScrollNext(inventoryModel);
                if (hasPrev || hasNext) {
                    constexpr float kNavScale = 0.84f;
                    const std::string prevLabel = runtime::backend_inventory::prevPageLabel();
                    const std::string nextLabel = runtime::backend_inventory::nextPageLabel();
                    appendText(20.0f,
                               invY,
                               prevLabel,
                               kNavScale,
                               hasPrev ? glm::vec3(0.75f, 0.87f, 0.96f)
                                       : glm::vec3(0.42f, 0.48f, 0.55f));
                    if (hasPrev) {
                        runtime::backend_inventory_panel::HitRegion prevHit;
                        prevHit.action = runtime::backend_inventory_panel::HitAction::ScrollOffset;
                        prevHit.offsetDelta = -1;
                        prevHit.x = 20.0f;
                        prevHit.y = invY;
                        prevHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(prevLabel, kNavScale));
                        prevHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(prevLabel, kNavScale));
                        backendInventoryPanel.hitRegions.push_back(std::move(prevHit));
                    }

                    const float nextX = 20.0f + std::max(1.0f, runtime::backend_text::measureTextWidth(prevLabel, kNavScale)) + 12.0f;
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
                    invY += 14.0f;
                }

                for (const auto& row : inventoryModel.rows) {
                    constexpr float kItemScale = 0.95f;
                    appendText(20.0f,
                               invY,
                               row.line,
                               kItemScale,
                               row.selected
                                   ? glm::vec3(0.98f, 0.90f, 0.58f)
                                   : glm::vec3(0.84f, 0.90f, 0.97f));
                    runtime::backend_inventory_panel::HitRegion hit;
                    hit.action = runtime::backend_inventory_panel::HitAction::SelectItem;
                    hit.itemId = row.itemId;
                    hit.x = 20.0f;
                    hit.y = invY;
                    hit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(row.line, kItemScale));
                    hit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(row.line, kItemScale));
                    backendInventoryPanel.hitRegions.push_back(std::move(hit));
                    invY += 15.0f;
                }

                const std::string clearLine = runtime::backend_inventory::clearSelectionLabel();
                appendText(20.0f,
                           invY + 1.0f,
                           clearLine,
                           0.90f,
                           selectedItem.empty()
                               ? glm::vec3(0.62f, 0.68f, 0.76f)
                               : glm::vec3(0.95f, 0.78f, 0.66f));
                runtime::backend_inventory_panel::HitRegion clearHit;
                clearHit.action = runtime::backend_inventory_panel::HitAction::ClearSelection;
                clearHit.itemId.clear();
                clearHit.x = 20.0f;
                clearHit.y = invY + 1.0f;
                clearHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(clearLine, 0.90f));
                clearHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(clearLine, 0.90f));
                backendInventoryPanel.hitRegions.push_back(std::move(clearHit));
                invY += 16.0f;
                appendText(20.0f,
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

                float typeY = 128.0f;
                appendRightText(typeY, "Type Lines", 1.0f, glm::vec3(0.98f, 0.90f, 0.60f));
                typeY += 16.0f;
                const std::size_t maxRows = std::min<std::size_t>(6, typeCounts.size());
                for (std::size_t i = 0; i < maxRows; ++i) {
                    appendRightText(typeY,
                                    runtime::hud::formatTypeLineEntry(typeCounts[i].type, typeCounts[i].uniqueLineCount),
                                    0.95f,
                                    glm::vec3(0.92f, 0.94f, 0.98f));
                    typeY += 15.0f;
                }
            }

            const auto& benchUnits = gameWorld->getBenchPokemons();
            if (!benchUnits.empty()) {
                float benchY = 252.0f;
                appendText(20.0f, benchY, "Bench", 1.0f, glm::vec3(0.86f, 0.94f, 0.98f));
                benchY += 16.0f;
                const std::size_t maxRows = std::min<std::size_t>(5, benchUnits.size());
                for (std::size_t i = 0; i < maxRows; ++i) {
                    appendText(20.0f,
                               benchY,
                               runtime::hud::formatUnitEntry(benchUnits[i].name, benchUnits[i].level),
                               0.95f,
                               glm::vec3(0.80f, 0.88f, 0.96f));
                    benchY += 15.0f;
                }
            }

            const auto& shopCards = gameWorld->getClassicShopCards();
            if (!shopCards.empty()) {
                float shopY = 252.0f;
                appendRightText(shopY, "Shop Offers", 1.0f, glm::vec3(0.98f, 0.90f, 0.60f));
                shopY += 16.0f;
                const std::size_t maxRows = std::min<std::size_t>(5, shopCards.size());
                for (std::size_t i = 0; i < maxRows; ++i) {
                    appendRightText(shopY,
                                    runtime::hud::formatShopCardEntry(shopCards[i].name,
                                                                      shopCards[i].level,
                                                                      shopCards[i].cost),
                                    0.95f,
                                    glm::vec3(0.92f, 0.94f, 0.98f));
                    shopY += 15.0f;
                }
            }
        }

        const auto recentMain = log.recentMainLines(7);
        if (!recentMain.empty()) {
            float y = std::max(142.0f, static_cast<float>(drawableH) - 170.0f);
            for (const auto& line : recentMain) {
                appendText(20.0f,
                           y,
                           trimDebugLine(line.text, 84),
                           1.0f,
                           glm::vec3(
                               std::clamp(line.color.r, 0.0f, 1.0f),
                               std::clamp(line.color.g, 0.0f, 1.0f),
                               std::clamp(line.color.b, 0.0f, 1.0f)));
                y += 16.0f;
            }
        }

        const bool classicMode = (mode == "classic");
        const auto sideLines = classicMode ? log.recentEconomyLines(5) : log.recentCatchLines(5);
        if (!sideLines.empty()) {
            float y = std::max(142.0f, static_cast<float>(drawableH) - 170.0f);
            for (const auto& line : sideLines) {
                const std::string text = trimDebugLine(line.text, 54);
                const float scale = 1.0f;
                const float textW = std::max(1.0f, runtime::backend_text::measureTextWidth(text, scale));
                const float x = std::max(20.0f, static_cast<float>(drawableW) - textW - 22.0f);
                appendText(x,
                           y,
                           text,
                           scale,
                           glm::vec3(
                               std::clamp(line.color.r, 0.0f, 1.0f),
                               std::clamp(line.color.g, 0.0f, 1.0f),
                               std::clamp(line.color.b, 0.0f, 1.0f)));
                y += 16.0f;
            }
        }

        if (!quads.empty()) {
            renderer->drawDebugQuads(quads.data(), quads.size(), drawableW, drawableH);
        }
        if (!lines.empty()) {
            renderer->drawDebugLines(lines.data(), lines.size(), drawableW, drawableH);
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

