// src/game/GamePreload.h
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

// Compatibility overload (keeps existing call sites working).
bool loadModelPathsFromConfig(const std::string& configPath, std::vector<std::string>& outPaths);

// Performs the actual preload loop (progress bar + model loads).
void preloadModels(GameContext& ctx,
                   const std::vector<std::string>& modelsToPreload,
                   const std::string& appName);

// High-level helper used by GameRuntime: read config (or fallback), then preload models with a progress UI.
void preloadCommonModels(GameContext& ctx,
                         const PokemonConfigLoader& pokemonCfg,
                         const std::string& appName = "PokemonAutochess");

// Convenience overload for legacy code that still uses the singleton loader.
void preloadCommonModels(GameContext& ctx, const std::string& appName = "PokemonAutochess");

} // namespace game::preload
