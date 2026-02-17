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

        if (renderWorldForInput && event.type == InputEvent::Type::MouseWheel) {
            itemInventoryUI.handleScroll(event.wheelY, viewport.height);
        }
        if (renderWorldForInput && event.type == InputEvent::Type::MouseDown &&
            event.mouseButtonId == InputEvent::MouseButton::Left) {
            if (auto clicked = itemInventoryUI.handleMouseClick(event.mouseX, event.mouseY)) {
                if (gameWorld) {
                    gameWorld->setSelectedItem(*clicked);
                    log.catchInfo("Selected " + *clicked + ". Click a target.");
                }
                return; // consume click (avoid dragging/other UI)
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

        std::vector<IRenderBackend::DebugQuad> quads;
        quads.reserve(1024);

        const int rows = std::max(1, config.rows);
        const int cols = std::max(1, config.cols);
        const float minDim = static_cast<float>(std::min(drawableW, drawableH));
        const float boardW = std::max(240.0f, minDim * 0.78f);
        const float boardH = std::max(180.0f, minDim * 0.58f);
        const float boardX = (static_cast<float>(drawableW) - boardW) * 0.5f;
        const float boardY = (static_cast<float>(drawableH) - boardH) * 0.5f;
        const float cellW = boardW / static_cast<float>(cols);
        const float cellH = boardH / static_cast<float>(rows);

        if (renderWorld) {
            IRenderBackend::DebugQuad boardBg;
            boardBg.x = boardX;
            boardBg.y = boardY;
            boardBg.w = boardW;
            boardBg.h = boardH;
            boardBg.r = 0.10f;
            boardBg.g = 0.16f;
            boardBg.b = 0.20f;
            boardBg.a = 1.0f;
            quads.push_back(boardBg);

            const float line = std::max(1.0f, minDim * 0.002f);
            for (int c = 0; c <= cols; ++c) {
                IRenderBackend::DebugQuad vLine;
                vLine.x = boardX + cellW * static_cast<float>(c) - line * 0.5f;
                vLine.y = boardY;
                vLine.w = line;
                vLine.h = boardH;
                vLine.r = 0.22f;
                vLine.g = 0.33f;
                vLine.b = 0.40f;
                vLine.a = 1.0f;
                quads.push_back(vLine);
            }
            for (int r = 0; r <= rows; ++r) {
                IRenderBackend::DebugQuad hLine;
                hLine.x = boardX;
                hLine.y = boardY + cellH * static_cast<float>(r) - line * 0.5f;
                hLine.w = boardW;
                hLine.h = line;
                hLine.r = 0.22f;
                hLine.g = 0.33f;
                hLine.b = 0.40f;
                hLine.a = 1.0f;
                quads.push_back(hLine);
            }

            if (gameWorld) {
                const auto& units = gameWorld->getPokemons();
                for (const auto& unit : units) {
                    if (!unit.alive && !unit.captureInProgress && !unit.fainting) continue;
                    const auto cell = gameWorld->worldToGrid(unit.position);
                    if (cell.x < 0 || cell.y < 0 || cell.x >= cols || cell.y >= rows) continue;

                    IRenderBackend::DebugQuad u;
                    u.x = boardX + cellW * static_cast<float>(cell.x) + cellW * 0.20f;
                    u.y = boardY + cellH * static_cast<float>(cell.y) + cellH * 0.20f;
                    u.w = cellW * 0.60f;
                    u.h = cellH * 0.60f;

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
                    u.a = 1.0f;
                    quads.push_back(u);
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

        if (!quads.empty()) {
            renderer->drawDebugQuads(quads.data(), quads.size(), drawableW, drawableH);
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
        if (!renderEnabled) {
            if (stateManager) stateManager->render();
            return;
        }
        if (!legacyRenderPath) {
            renderBackendDebugView(drawableW, drawableH, renderWorld);
            if (stateManager) stateManager->render();
            return;
        }
        if (renderWorld) {
            if (board && gameWorld) {
                board->setCellSize(gameWorld->getBoardCellSize());
            }
            if (gameWorld && camera && board) gameWorld->drawAll(*camera, *board);
            if (gameWorld && camera) {
                auto healthBarData = gameWorld->getHealthBarData(*camera, drawableW, drawableH);
                healthBarRenderer.render(healthBarData);
            }
        }
        if (stateManager) stateManager->render();

        if (renderWorld) {
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

