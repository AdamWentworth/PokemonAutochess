// tests/TestContentValidation.cpp
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "engine/core/Paths.h"
#include "game/config/PokemonConfigLoader.h"
#include "game/config/MovesConfigLoader.h"

static bool check_move_kind(const MoveData* move, const std::string& expected, std::string& outFail) {
    if (!move) return false;
    if (move->kind != expected) {
        outFail = "Move kind mismatch: expected " + expected + ", got " + move->kind;
        return false;
    }
    return true;
}

bool test_content_invariants(std::string& outFail) {
    PokemonConfigLoader pokemon;
    MovesConfigLoader moves;

    const std::string pokemonPath = engine::paths::data("config/pokemon_config.json");
    const std::string movesPath = engine::paths::data("config/moves_config.json");

    if (!moves.loadConfig(movesPath, nullptr)) {
        outFail = "Failed to load moves config: " + movesPath;
        return false;
    }
    if (!pokemon.loadConfig(pokemonPath, nullptr)) {
        outFail = "Failed to load pokemon config: " + pokemonPath;
        return false;
    }

    const std::string growlManifestPath =
        engine::paths::data("config/vfx/moves/growl_draw_passes.json");
    const std::string deprecatedDirectionalAliasPath =
        engine::paths::data("config/vfx/moves/directional_sound_rings_draw_passes.json");

    auto loadManifest = [&](const std::string& path, nlohmann::json& outJson) -> bool {
        std::ifstream in(path);
        if (!in.is_open()) return false;
        try {
            in >> outJson;
        } catch (...) {
            return false;
        }
        return true;
    };

    if (!std::filesystem::exists(growlManifestPath)) {
        outFail = "Missing growl VFX manifest: " + growlManifestPath;
        return false;
    }
    if (std::filesystem::exists(deprecatedDirectionalAliasPath)) {
        outFail = "Deprecated duplicate growl VFX manifest should be removed: " + deprecatedDirectionalAliasPath;
        return false;
    }

    nlohmann::json growlManifest;
    if (!loadManifest(growlManifestPath, growlManifest)) {
        outFail = "Failed to parse growl VFX manifest: " + growlManifestPath;
        return false;
    }
    if (!growlManifest.contains("draw_passes") || !growlManifest["draw_passes"].is_array() ||
        growlManifest["draw_passes"].empty()) {
        outFail = "Growl VFX manifest must define non-empty draw_passes.";
        return false;
    }

    for (const auto& [name, stats] : pokemon.all()) {
        for (const auto& [level, loadout] : stats.loadoutByLevel) {
            bool hasAny = false;
            if (!loadout.fast.empty()) {
                hasAny = true;
                const MoveData* move = moves.getMove(loadout.fast);
                if (!move) {
                    outFail = "Missing fast move '" + loadout.fast + "' for " + name + " at level " + std::to_string(level);
                    return false;
                }
                if (!check_move_kind(move, "fast", outFail)) {
                    outFail = "Fast move kind mismatch for " + name + " at level " + std::to_string(level) +
                              ": " + loadout.fast + " (" + outFail + ")";
                    return false;
                }
            }
            if (!loadout.charged.empty()) {
                hasAny = true;
                const MoveData* move = moves.getMove(loadout.charged);
                if (!move) {
                    outFail = "Missing charged move '" + loadout.charged + "' for " + name + " at level " + std::to_string(level);
                    return false;
                }
                if (!check_move_kind(move, "charged", outFail)) {
                    outFail = "Charged move kind mismatch for " + name + " at level " + std::to_string(level) +
                              ": " + loadout.charged + " (" + outFail + ")";
                    return false;
                }
            }

            if (!hasAny) {
                outFail = "Loadout has no moves for " + name + " at level " + std::to_string(level);
                return false;
            }
        }
    }

    return true;
}
