// src/game/GameRuntime.h
#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <filesystem>
#include <fstream>

// Compatibility helpers for projects not building with C++20/23.
static inline bool str_starts_with(const std::string& s, const char* prefix) {
    const size_t n = std::char_traits<char>::length(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

#include <nlohmann/json.hpp>

#include "engine/core/GameContext.h"
#include "engine/core/EngineServices.h"
#include "engine/core/Paths.h"
#include "engine/input/InputEvent.h"

#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"
#include "engine/utils/ResourceManager.h"

#include "engine/ui/UIManager.h"
#include "engine/ui/BattleFeed.h"
#include "engine/ui/HealthBarRenderer.h"

#include "engine/core/SystemRegistry.h"

#include "game/GameWorld.h"
#include "game/GameStateManager.h"

#include "game/config/PokemonConfigLoader.h"
#include "game/config/MovesConfigLoader.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/FlyerConfigLoader.h"
#include "game/config/GameDataDb.h"

#include "game/systems/CameraSystem.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/ShopSystem.h"

#include "game/state/ScriptedState.h"
#include "game/GameServices.h"
#include "game/GameConfig.h"

#include "game/logging/LogBus.h"

namespace game_runtime_detail {

inline const char* phaseName(RoundPhase p) {
    switch (p) {
        case RoundPhase::Planning:    return "Planning";
        case RoundPhase::Battle:      return "Battle";
        case RoundPhase::Resolution:  return "Resolution";
        default:                      return "Unknown";
    }
}

inline void addByPokemonName(std::vector<std::string>& out, const std::string& name, const PokemonConfigLoader& pokemonCfg) {
    const PokemonStats* stats = pokemonCfg.getStats(name);
    if (!stats) return;
    out.push_back(std::string("assets/models/") + stats->model);
}

inline std::vector<std::string> loadPreloadListFromJsonOrFallback(const PokemonConfigLoader& pokemonCfg) {
    // Config lives under PAC_DATA_ROOT/config by default (consistent with other configs).
    const std::filesystem::path cfgPath = engine::paths::data("config/preload_models.json");

    std::vector<std::string> out;
    out.reserve(32);

    // Fallback list (matches prior hardcoded behavior).
    const auto fallback = [&]() {
        addByPokemonName(out, "bulbasaur", pokemonCfg);
        addByPokemonName(out, "charmander", pokemonCfg);
        addByPokemonName(out, "squirtle", pokemonCfg);
        addByPokemonName(out, "pidgey", pokemonCfg);
        addByPokemonName(out, "rattata", pokemonCfg);
    };

    std::ifstream f(cfgPath);
    if (!f.good()) {
        fallback();
        return out;
    }

    try {
        nlohmann::json j;
        f >> j;

        const std::string modelRoot = j.value("model_root", "assets/models/");

        if (j.contains("pokemon") && j["pokemon"].is_array()) {
            for (const auto& v : j["pokemon"]) {
                if (!v.is_string()) continue;
                addByPokemonName(out, v.get<std::string>(), pokemonCfg);
            }
        }

        if (j.contains("models") && j["models"].is_array()) {
            for (const auto& v : j["models"]) {
                if (!v.is_string()) continue;
                std::string path = v.get<std::string>();
                // If user supplies bare filename, resolve relative to model_root.
                if (!str_starts_with(path, "assets/") && !str_starts_with(path, "data/") && !(path.find(':') != std::string::npos)) {
                    path = modelRoot + path;
                }
                out.push_back(path);
            }
        }

        if (out.empty()) {
            // Avoid silent "no preload" because of a bad file.
            fallback();
        }
    } catch (const std::exception& e) {
        std::cerr << "[Preload] Failed to parse preload_models.json: " << e.what() << "\n";
        out.clear();
        fallback();
    }

    return out;
}

inline void preloadCommonModels(GameContext& ctx) {
    // IMPORTANT: must be called AFTER GL context + glad are ready.
    std::vector<std::string> modelsToPreload =
    loadPreloadListFromJsonOrFallback(PokemonConfigLoader::getInstance());
    if (modelsToPreload.empty()) return;

    if (ctx.setTitle) ctx.setTitle("PokemonAutochess - Loading.");

    // draw initial bar
    if (ctx.renderBootLoading) ctx.renderBootLoading(0.0f);

    if (ctx.pumpPreloadEvents && !ctx.pumpPreloadEvents()) {
        if (ctx.requestQuit) ctx.requestQuit();
        return;
    }

    const int total = static_cast<int>(modelsToPreload.size());
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
            std::cerr << "[Preload] No resource service available; skipping model load.\n";
        }

        if (ctx.renderBootLoading) {
            const float t = (total > 0) ? float(i + 1) / float(total) : 1.0f;
            ctx.renderBootLoading(t);
        }
    }

    if (ctx.setTitle) ctx.setTitle("PokemonAutochess - Loading done.");
}

} // namespace game_runtime_detail

struct GameRuntime {
    // Pointers (engine-owned)
    Camera3D* camera = nullptr;

    // Owned state
    std::unique_ptr<GameStateManager> stateManager;
    std::unique_ptr<GameWorld>        gameWorld;
    std::unique_ptr<BoardRenderer>    board;
    std::unique_ptr<BattleFeed>       battleFeed;

    // Threaded into GameWorld to avoid gameplay code calling config singletons.
    GameDataDb dataDb;

    // Game-owned logger instance (no file-scope globals).
    LogBus::Logger log;

    // v1: thread config/db/log into states without singletons.
    std::unique_ptr<GameServices> services;

    HealthBarRenderer healthBarRenderer;
    SystemRegistry systemRegistry;

    std::shared_ptr<CameraSystem>          cameraSystem;
    std::shared_ptr<UnitInteractionSystem> unitSystem;
    std::shared_ptr<ShopSystem>            shopSystem;
    std::shared_ptr<RoundSystem>           roundSystem;

    RoundPhase lastRoundPhase = RoundPhase::Planning;
    bool hasLastRoundPhase = false;

    void init(GameContext& ctx) {
        // Load configs (game-specific) from data root (PAC_DATA_ROOT), not CWD-sensitive literals.
        PokemonConfigLoader::getInstance().loadConfig(engine::paths::data("config/pokemon_config.json"));
        MovesConfigLoader::getInstance().loadConfig(engine::paths::data("config/moves_config.json"));
        AttackAnimConfigLoader::getInstance().loadConfig(engine::paths::data("config/attack_anim_config.json"));
        FlyerConfigLoader::getInstance().loadConfig(engine::paths::data("config/flyers_config.json"));

        dataDb.pokemon     = &PokemonConfigLoader::getInstance();
        dataDb.moves       = &MovesConfigLoader::getInstance();
        dataDb.attackAnims = &AttackAnimConfigLoader::getInstance();
        dataDb.flyers      = &FlyerConfigLoader::getInstance();

        std::cout << "[Init] CWD: " << std::filesystem::current_path() << "\n";
        std::cout << "[Init] PAC_DATA_ROOT: " << engine::paths::dataRoot() << "\n";
        std::cout << "[Init] PAC_ASSET_ROOT: " << engine::paths::assetRoot() << "\n";

        camera = ctx.camera;

        const auto& cfg = GameConfig::get();
        if (!services) services = std::make_unique<GameServices>(cfg, dataDb, log);
        board = std::make_unique<BoardRenderer>(cfg.rows, cfg.cols, cfg.cellSize);

        gameWorld = std::make_unique<GameWorld>();
        if (ctx.services) gameWorld->setResources(ctx.services->resources);
        gameWorld->setData(&dataDb);

        stateManager = std::make_unique<GameStateManager>();

        cameraSystem = std::make_shared<CameraSystem>(camera);
        unitSystem   = std::make_shared<UnitInteractionSystem>(camera, gameWorld.get(), ctx.drawableW, ctx.drawableH);

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

        game_runtime_detail::preloadCommonModels(ctx);

        stateManager->pushState(std::make_unique<ScriptedState>(
            stateManager.get(),
            gameWorld.get(),
            *services,
            engine::paths::data("scripts/states/starter.lua")
        ));

        if (ctx.setTitle) ctx.setTitle("Pokemon Autochess");
        std::cout << "[Init] Game initialized.\n";
    }

    void handleEvent(const InputEvent& event) {
        if (cameraSystem) cameraSystem->handleInput(event);
        if (unitSystem)   unitSystem->handleInput(event);
        if (shopSystem)   shopSystem->handleInput(event);
        if (stateManager) stateManager->handleInput(event);
    }

    void fixedUpdate(float dt) {
        systemRegistry.updateAll(dt);

        if (roundSystem && shopSystem) {
            const RoundPhase current = roundSystem->getCurrentPhase();
            if (!hasLastRoundPhase) {
                lastRoundPhase = current;
                hasLastRoundPhase = true;
            } else if (current != lastRoundPhase) {
                shopSystem->onRoundPhaseChanged(lastRoundPhase, current);

                LogBus::colored(
                    std::string("Phase: ") + game_runtime_detail::phaseName(lastRoundPhase) +
                        " \xE2\x86\x92 " + game_runtime_detail::phaseName(current),
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

    void render(int drawableW, int drawableH) {
        if (board && camera) board->draw(*camera);
        if (gameWorld && camera && board) gameWorld->drawAll(*camera, *board);
        if (stateManager) stateManager->render();

        if (gameWorld && camera) {
            auto healthBarData = gameWorld->getHealthBarData(*camera, drawableW, drawableH);
            healthBarRenderer.render(healthBarData);
        }

        if (shopSystem) shopSystem->renderUI(drawableW, drawableH);
        if (battleFeed) battleFeed->render(drawableW, drawableH);
    }

    void shutdown() {
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
};