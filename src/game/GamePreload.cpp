// src/game/GamePreload.cpp
#include "game/GamePreload.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/GameContext.h"
#include "engine/core/Paths.h"
#include "engine/core/EngineServices.h"
#include "engine/utils/ResourceManager.h"

#include "game/config/PokemonConfigLoader.h"

namespace game::preload {

bool loadModelPathsFromConfig(const std::string& configPath,
                             const PokemonConfigLoader& pokemonCfg,
                             std::vector<std::string>& outPaths) {
    outPaths.clear();

    std::ifstream in(configPath);
    if (!in.is_open()) {
        // Missing file should not be fatal (we fall back to hardcoded list).
        std::cerr << "[Preload] Could not open preload config: " << configPath << "\n";
        return false;
    }

    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        std::cerr << "[Preload] Failed to parse JSON (" << configPath << "): " << e.what() << "\n";
        return false;
    }

    if (!j.is_object()) {
        std::cerr << "[Preload] Preload config is not a JSON object: " << configPath << "\n";
        return false;
    }

    const std::string modelRoot = j.value("model_root", std::string("assets/models/"));

    std::vector<std::string> pokemonNames;
    std::vector<std::string> modelEntries;

    if (j.contains("pokemon") && j["pokemon"].is_array()) {
        for (const auto& v : j["pokemon"]) {
            if (v.is_string()) pokemonNames.push_back(v.get<std::string>());
        }
    }
    if (j.contains("models") && j["models"].is_array()) {
        for (const auto& v : j["models"]) {
            if (v.is_string()) modelEntries.push_back(v.get<std::string>());
        }
    }

    std::unordered_set<std::string> seen;
    auto pushUnique = [&](const std::string& p) {
        if (p.empty()) return;
        if (seen.insert(p).second) outPaths.push_back(p);
    };

    // Expand pokemon names -> model paths using PokemonConfigLoader.
    for (const std::string& name : pokemonNames) {
        const PokemonStats* stats = pokemonCfg.getStats(name);
        if (!stats) {
            std::cerr << "[Preload] Unknown pokemon in preload config: " << name << "\n";
            continue;
        }
        pushUnique(modelRoot + stats->model);
    }

    // Allow explicit model paths (either full "assets/..." or relative to model_root).
    for (const std::string& entry : modelEntries) {
        if (entry.rfind("assets/", 0) == 0) {
            pushUnique(entry);
        } else if (entry.size() >= 3 && std::isalpha((unsigned char)entry[0]) && entry[1] == ':' &&
                   (entry[2] == '\\' || entry[2] == '/')) {
            // Windows absolute path, keep as-is.
            pushUnique(entry);
        } else if (!entry.empty() && (entry[0] == '/' || entry[0] == '\\')) {
            // POSIX absolute path, keep as-is.
            pushUnique(entry);
        } else {
            pushUnique(modelRoot + entry);
        }
    }

    return !outPaths.empty();
}

void preloadModels(GameContext& ctx,
                   const std::vector<std::string>& modelsToPreload,
                   const std::string& appName) {
    if (modelsToPreload.empty()) return;

    if (ctx.setTitle) ctx.setTitle(appName + " - Loading.");

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
                appName + " - Loading " +
                std::to_string(i + 1) + "/" + std::to_string(total) + "  " + path
            );
        }

        if (ctx.pumpPreloadEvents && !ctx.pumpPreloadEvents()) {
            if (ctx.requestQuit) ctx.requestQuit();
            return;
        }

        // Expensive load (engine-owned service)
        if (ctx.services && ctx.services->resources) {
            ctx.services->resources->getModel(path);
        } else {
            std::cerr << "[Preload] No resource service available; skipping model preload for: " << path << "\n";
        }

        const float progress = float(i + 1) / float(total);
        if (ctx.renderBootLoading) ctx.renderBootLoading(progress);
    }

    if (ctx.setTitle) ctx.setTitle("Pokemon Autochess");
    if (ctx.pumpPreloadEvents) ctx.pumpPreloadEvents();
}

void preloadCommonModels(GameContext& ctx,
                         const PokemonConfigLoader& pokemonCfg,
                         const std::string& appName) {
    std::vector<std::string> modelsToPreload;

    const std::string cfgPath = engine::paths::data("config/preload_models.json");
    const bool ok = loadModelPathsFromConfig(cfgPath, pokemonCfg, modelsToPreload);

    if (!ok) {
        // Fallback list (mirrors the original behavior).
        auto addByPokemonName = [&](const char* name) {
            const PokemonStats* stats = pokemonCfg.getStats(name);
            if (!stats) return;
            modelsToPreload.push_back(std::string("assets/models/") + stats->model);
        };
        addByPokemonName("bulbasaur");
        addByPokemonName("charmander");
        addByPokemonName("squirtle");
        addByPokemonName("pidgey");
        addByPokemonName("rattata");
    }

    preloadModels(ctx, modelsToPreload, appName);
}

} // namespace game::preload
