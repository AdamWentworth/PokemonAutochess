// src/game/GameApp.cpp

#include "game/GameApp.h"

#include "engine/core/GameContext.h"
#include "engine/core/EngineServices.h"
#include "engine/core/Paths.h"
#include "engine/input/InputEvent.h"

#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"
#include "engine/utils/ResourceManager.h"

#include "engine/ui/UIManager.h"
#include "engine/ui/BattleFeed.h"

#include "game/GameWorld.h"
#include "game/GameStateManager.h"

#include "game/config/PokemonConfigLoader.h"
#include "game/config/MovesConfigLoader.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/FlyerConfigLoader.h"

#include "game/systems/CameraSystem.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/ShopSystem.h"

#include "game/state/ScriptedState.h"

#include "game/GameConfig.h"

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
    // Load configs (game-specific) from data root (PAC_DATA_ROOT), not CWD-sensitive literals.
    PokemonConfigLoader::getInstance().loadConfig(engine::paths::data("config/pokemon_config.json"));
    MovesConfigLoader::getInstance().loadConfig(engine::paths::data("config/moves_config.json"));
    AttackAnimConfigLoader::getInstance().loadConfig(engine::paths::data("config/attack_anim_config.json"));
    FlyerConfigLoader::getInstance().loadConfig(engine::paths::data("config/flyers_config.json"));

    dataDb.pokemon = &PokemonConfigLoader::getInstance();
    dataDb.moves = &MovesConfigLoader::getInstance();
    dataDb.attackAnims = &AttackAnimConfigLoader::getInstance();
    dataDb.flyers = &FlyerConfigLoader::getInstance();

    std::cout << "[Init] CWD: " << std::filesystem::current_path() << "\n";
    std::cout << "[Init] PAC_DATA_ROOT: " << engine::paths::dataRoot() << "\n";
    std::cout << "[Init] PAC_ASSET_ROOT: " << engine::paths::assetRoot() << "\n";

    camera = ctx.camera;

    const auto& cfg = GameConfig::get();
    board = std::make_unique<BoardRenderer>(cfg.rows, cfg.cols, cfg.cellSize);

    gameWorld    = std::make_unique<GameWorld>();
    if (ctx.services) gameWorld->setResources(ctx.services->resources);
    gameWorld->setData(&dataDb);
    stateManager = std::make_unique<GameStateManager>();

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

    if (roundSystem && shopSystem) {
        lastRoundPhase = roundSystem->getCurrentPhase();
        hasLastRoundPhase = true;
        shopSystem->onRoundPhaseChanged(lastRoundPhase, lastRoundPhase);
    }

    healthBarRenderer.init();

    // Battle feed + logger (instance-based)
    battleFeed = std::make_unique<BattleFeed>(cfg.fontPath, cfg.fontSize);
    log.attach(battleFeed.get());
    log.setEchoToStdout(false);
    LogBus::setActive(&log);

    preloadCommonModels(ctx);

    stateManager->pushState(std::make_unique<ScriptedState>(
        stateManager.get(),
        gameWorld.get(),
        engine::paths::data("scripts/states/starter.lua")
    ));

    if (ctx.setTitle) ctx.setTitle("Pokemon Autochess");
    std::cout << "[Init] Game initialized.\n";
}

void GameApp::handleEvent(const InputEvent& event) {
    if (cameraSystem) cameraSystem->handleInput(event);
    if (unitSystem)   unitSystem->handleInput(event);
    if (shopSystem)   shopSystem->handleInput(event);
    if (stateManager) stateManager->handleInput(event);
}

void GameApp::fixedUpdate(float dt) {
    systemRegistry.updateAll(dt);

    if (roundSystem && shopSystem) {
        const RoundPhase current = roundSystem->getCurrentPhase();
        if (!hasLastRoundPhase) {
            lastRoundPhase = current;
            hasLastRoundPhase = true;
        } else if (current != lastRoundPhase) {
            shopSystem->onRoundPhaseChanged(lastRoundPhase, current);

            LogBus::colored(
                std::string("Phase: ") + phaseName(lastRoundPhase) + " \xE2\x86\x92 " + phaseName(current),
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

    LogBus::setActive(nullptr);
    log.attach(nullptr);

    if (board) {
        board->shutdown();
        board.reset();
    }

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

    if (ctx.pumpPreloadEvents && !ctx.pumpPreloadEvents()) {
        if (ctx.requestQuit) ctx.requestQuit();
        return;
    }

    const int total = (int)modelsToPreload.size();
    for (int i = 0; i < total; ++i) {
        const std::string& path = modelsToPreload[i];

        if (ctx.setTitle) {
            ctx.setTitle(
                "PokemonAutochess - Loading " +
                std::to_string(i + 1) + "/" + std::to_string(total) + "  " + path
            );
        }

        if (ctx.pumpPreloadEvents && !ctx.pumpPreloadEvents()) {
        if (ctx.requestQuit) ctx.requestQuit();
        return;
    }

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