// src/game/GameApp.cpp

#include "game/GameApp.h"

#include "engine/core/GameContext.h"
#include "engine/core/EngineServices.h"
#include "engine/input/InputEvent.h"

#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"
#include "engine/utils/ResourceManager.h"

#include "engine/ui/UIManager.h"
#include "engine/ui/BattleFeed.h"

#include "game/GameWorld.h"
#include "game/GameStateManager.h"
#include "game/PokemonConfigLoader.h"
#include "game/MovesConfigLoader.h"
#include "game/AttackAnimConfigLoader.h"
#include "game/FlyerConfigLoader.h"
#include "game/systems/CameraSystem.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/ShopSystem.h"
#include "game/ScriptedState.h"
#include "game/GameConfig.h"
#include "game/LogBus.h"

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

static const char* phaseName(RoundPhase p) {
    switch (p) {
        case RoundPhase::Planning: return "Planning";
        case RoundPhase::Battle:   return "Battle";
        case RoundPhase::Resolution:     return "Resolution";
        default:                   return "Unknown";
    }
}

GameApp::GameApp() = default;
GameApp::~GameApp() = default;

void GameApp::init(GameContext& ctx) {
    // Load configs (game-specific)
    PokemonConfigLoader::getInstance().loadConfig("config/pokemon_config.json");
    MovesConfigLoader::getInstance().loadConfig("config/moves_config.json");
    // Attack animation clips for fast/charged moves (e.g. Bulbasaur vine_whip start/loop/end)
    AttackAnimConfigLoader::getInstance().loadConfig("config/attack_anim_config.json");
    // Flyers list (used to enable visual-only flight locomotion on specific species)
    FlyerConfigLoader::getInstance().loadConfig("config/flyers_config.json");

    std::cout << "[Init] CWD: " << std::filesystem::current_path() << "\n";

    camera = ctx.camera;

    const auto& cfg = GameConfig::get();
    board = std::make_unique<BoardRenderer>(cfg.rows, cfg.cols, cfg.cellSize);

    gameWorld    = std::make_unique<GameWorld>();
    // Thread engine-owned resource service into the world (no singleton access in gameplay).
    if (ctx.services) gameWorld->setResources(ctx.services->resources);
    stateManager = std::make_unique<GameStateManager>();

    // Systems (game-specific)
    cameraSystem = std::make_shared<CameraSystem>(camera);
    unitSystem   = std::make_shared<UnitInteractionSystem>(
        camera, gameWorld.get(), ctx.drawableW, ctx.drawableH
    );

    systemRegistry.registerSystem(cameraSystem);
    systemRegistry.registerSystem(unitSystem);

    roundSystem = std::make_shared<RoundSystem>();
    systemRegistry.registerSystem(roundSystem);

    shopSystem = std::make_shared<ShopSystem>();
    systemRegistry.registerSystem(shopSystem);

    // Initialize phase-dependent UI once at startup (roll shop if we start in Planning).
    if (roundSystem && shopSystem) {
        lastRoundPhase = roundSystem->getCurrentPhase();
        hasLastRoundPhase = true;
        shopSystem->onRoundPhaseChanged(lastRoundPhase, lastRoundPhase);
    }

    healthBarRenderer.init();

    // Battle feed + LogBus
    battleFeed = std::make_unique<BattleFeed>(cfg.fontPath, cfg.fontSize);
    LogBus::attach(battleFeed.get());

    // Console logging can stall badly on Windows in Debug.
    LogBus::setEchoToStdout(false);

    // Preload (game-level decision, uses engine loading UI via ctx)
    preloadCommonModels(ctx);

    stateManager->pushState(std::make_unique<ScriptedState>(
        stateManager.get(), gameWorld.get(), "scripts/states/starter.lua"));

    if (ctx.setTitle) ctx.setTitle("Pokemon Autochess");

    std::cout << "[Init] Game initialized.\n";
}

void GameApp::handleEvent(const InputEvent& event) {
    // Game-owned input handling. The engine only forwards InputEvent.
    if (cameraSystem) cameraSystem->handleInput(event);
    if (unitSystem)   unitSystem->handleInput(event);
    if (shopSystem)   shopSystem->handleInput(event);
    if (stateManager) stateManager->handleInput(event);
}

void GameApp::fixedUpdate(float dt) {
    systemRegistry.updateAll(dt);

    // Detect and react to round phase changes without a global EventManager singleton.
    if (roundSystem && shopSystem) {
        const RoundPhase current = roundSystem->getCurrentPhase();
        if (!hasLastRoundPhase) {
            lastRoundPhase = current;
            hasLastRoundPhase = true;
        } else if (current != lastRoundPhase) {
            shopSystem->onRoundPhaseChanged(lastRoundPhase, current);

            LogBus::colored(
                std::string("Phase: ") + phaseName(lastRoundPhase) + " → " + phaseName(current),
                {0.75f, 0.9f, 1.0f},
                3.0f
            );

            lastRoundPhase = current;
        }
    }

    if (stateManager) stateManager->update(dt);
    if (gameWorld)    gameWorld->update(dt);

    if (battleFeed)   battleFeed->update(dt);
}

void GameApp::render(int drawableW, int drawableH) {
    if (board && camera) {
        board->draw(*camera);
    }

    if (gameWorld && camera && board) {
        gameWorld->drawAll(*camera, *board);
    }

    if (stateManager) stateManager->render();

    if (gameWorld && camera) {
        auto healthBarData = gameWorld->getHealthBarData(*camera, drawableW, drawableH);
        healthBarRenderer.render(healthBarData);
    }

    if (shopSystem) shopSystem->renderUI(drawableW, drawableH);
    if (battleFeed) battleFeed->render(drawableW, drawableH);
}

void GameApp::shutdown() {
    std::cout << "[Shutdown] Game.\n";

    // Game-owned GL resources
    if (board) {
        board->shutdown();
        board.reset();
    }

    // Engine global UI manager
    UIManager::shutdown();

    battleFeed.reset();
    shopSystem.reset();
    unitSystem.reset();
    cameraSystem.reset();
    roundSystem.reset();

    stateManager.reset();
    gameWorld.reset();

    systemRegistry.clear();

    std::cout << "[Shutdown] Game done.\n";
}

void GameApp::preloadCommonModels(GameContext& ctx) {
    // Preload models that you know you will use early to avoid hitching mid-combat.
    // IMPORTANT: must be called AFTER GL context + glad are ready.

    auto addByName = [&](std::vector<std::string>& out, const std::string& name) {
        const PokemonStats* stats = PokemonConfigLoader::getInstance().getStats(name);
        if (!stats) return;
        const std::string path = "assets/models/" + stats->model;
        out.push_back(path);
    };

    std::vector<std::string> modelsToPreload;
    modelsToPreload.reserve(16);

    // starters
    addByName(modelsToPreload, "bulbasaur");
    addByName(modelsToPreload, "charmander");
    addByName(modelsToPreload, "squirtle");

    // route1 (from scripts/states/route1.lua)
    addByName(modelsToPreload, "pidgey");
    addByName(modelsToPreload, "rattata");

    if (modelsToPreload.empty()) return;

    if (ctx.setTitle) ctx.setTitle("PokemonAutochess - Loading.");

    // draw initial bar
    if (ctx.renderBootLoading) ctx.renderBootLoading(0.0f);

    if (ctx.pumpPreloadEvents && !ctx.pumpPreloadEvents()) std::exit(0);

    const int total = (int)modelsToPreload.size();
    for (int i = 0; i < total; ++i) {
        const std::string& path = modelsToPreload[i];

        if (ctx.setTitle) {
            ctx.setTitle(
                "PokemonAutochess - Loading " +
                std::to_string(i + 1) + "/" + std::to_string(total) + "  " + path
            );
        }

        if (ctx.pumpPreloadEvents && !ctx.pumpPreloadEvents()) std::exit(0);

        // expensive load (engine-owned service)
        if (ctx.services && ctx.services->resources) {
            ctx.services->resources->getModel(path);
        } else {
            std::cerr << "[Preload] No resource service available; skipping model preload for: " << path << "\n";
        }

        float progress = float(i + 1) / float(total);
        if (ctx.renderBootLoading) ctx.renderBootLoading(progress);
    }

    if (ctx.setTitle) ctx.setTitle("Pokemon Autochess");
    if (ctx.pumpPreloadEvents) ctx.pumpPreloadEvents();
}
