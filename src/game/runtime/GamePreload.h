// src/game/runtime/GamePreload.h
#pragma once

#include <string>
#include <vector>

struct GameContext;
class PokemonConfigLoader;

namespace game::preload {

// Returns true if we successfully read/parsed the config file and produced at least one path.
bool loadModelPathsFromConfig(const std::string& configPath,
                             const PokemonConfigLoader& pokemonCfg,
                             std::vector<std::string>& outPaths);

// Performs the actual preload loop (progress bar + model loads).
void preloadModels(GameContext& ctx,
                   const std::vector<std::string>& modelsToPreload,
                   const std::string& appName);

// High-level helper: read config (or fallback), then preload models with a progress UI.
void preloadCommonModels(GameContext& ctx,
                         const PokemonConfigLoader& pokemonCfg,
                         const std::string& appName = "PokemonAutochess");

} // namespace game::preload

