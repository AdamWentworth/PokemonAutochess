// tests/TestContentValidation.cpp
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "engine/core/Paths.h"
#include "game/config/PokemonConfigLoader.h"
#include "game/config/MovesConfigLoader.h"

static bool check_move_kind(const MoveData *move, const std::string &expected, std::string &outFail) {
    if (!move) return false;
    if (move->kind != expected) {
        outFail = "Move kind mismatch: expected " + expected + ", got " + move->kind;
        return false;
    }
    return true;
}

bool test_content_invariants(std::string &outFail) {
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

    const std::array<std::pair<const char*, const char*>, 9> starterModels{{
        {"bulbasaur", "0001_Bulbasaur_SV"},
        {"ivysaur", "0002_Ivysaur_SV"},
        {"venusaur", "0003_Venusaur_SV"},
        {"charmander", "0004_Charmander_SV"},
        {"charmeleon", "0005_Charmeleon_SV"},
        {"charizard", "0006_Charizard_SV"},
        {"squirtle", "0007_Squirtle_SV"},
        {"wartortle", "0008_Wartortle_SV"},
        {"blastoise", "0009_Blastoise_SV"},
    }};
    for (const auto& [species, stem] : starterModels) {
        const PokemonStats* stats = pokemon.getStats(species);
        if (!stats) {
            outFail = "Missing Kanto starter-family config: " + std::string(species);
            return false;
        }
        const std::string regular = std::string(stem) + ".phmodel";
        const std::string shiny = std::string(stem) + "_Shiny.phmodel";
        if (stats->model != regular ||
            stats->resolveModel("regular") != regular ||
            stats->resolveModel("shiny") != shiny) {
            outFail =
                "Kanto starter-family config must select native Scarlet/Violet regular and shiny models: " +
                std::string(species);
            return false;
        }
        const bool isVenusaur = std::string(species) == "venusaur";
        const bool hasFemaleModel = stats->modelVariants.find("female") != stats->modelVariants.end();
        const bool hasFemaleShinyModel =
            stats->modelVariants.find("female_shiny") != stats->modelVariants.end();
        if (hasFemaleModel != isVenusaur || hasFemaleShinyModel != isVenusaur) {
            outFail =
                "Only Venusaur should expose sex-specific Kanto starter-family model variants.";
            return false;
        }
    }

    const std::array<std::pair<const char*, const char*>, 2> pidgeyFamilyModels{{
        {"pidgey", "0016_Pidgey_ZA"},
        {"pidgeotto", "0017_Pidgeotto_ZA"},
    }};
    for (const auto& [species, stem] : pidgeyFamilyModels) {
        const PokemonStats* stats = pokemon.getStats(species);
        if (!stats) {
            outFail = "Missing Pidgey-family config: " + std::string(species);
            return false;
        }
        const std::string regular = std::string(stem) + ".phmodel";
        const std::string shiny = std::string(stem) + "_Shiny.phmodel";
        if (stats->model != regular ||
            stats->resolveModel("regular") != regular ||
            stats->resolveModel("shiny") != shiny) {
            outFail =
                "Pidgey-family config must select native Legends: Z-A regular and shiny models: " +
                std::string(species);
            return false;
        }
    }

    const PokemonStats* rattata = pokemon.getStats("rattata");
    if (!rattata ||
        rattata->model != "0019_Rattata_LGPE.phmodel" ||
        rattata->resolveModel("regular") != "0019_Rattata_LGPE.phmodel" ||
        rattata->resolveModel("shiny") !=
            "0019_Rattata_LGPE_Shiny.phmodel" ||
        rattata->resolveModel("female") !=
            "0019_Rattata_LGPE_Female.phmodel" ||
        rattata->resolveModel("female_shiny") !=
            "0019_Rattata_LGPE_Female_Shiny.phmodel") {
        outFail =
            "Rattata config must select native LGPE male/female regular and shiny models.";
        return false;
    }

    const PokemonStats* spearow = pokemon.getStats("spearow");
    if (!spearow ||
        spearow->model != "0021_Spearow_LGPE.phmodel" ||
        spearow->resolveModel("regular") !=
            "0021_Spearow_LGPE.phmodel" ||
        spearow->resolveModel("shiny") !=
            "0021_Spearow_LGPE_Shiny.phmodel") {
        outFail =
            "Spearow config must select native LGPE regular and shiny models.";
        return false;
    }

    const std::string growlManifestPath =
        engine::paths::data("config/vfx/moves/growl_draw_passes.json");
    const std::string deprecatedDirectionalAliasPath =
        engine::paths::data("config/vfx/moves/directional_sound_rings_draw_passes.json");

    auto loadManifest = [&](const std::string &path, nlohmann::json &outJson) -> bool {
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

    auto findGrowlPassForwardOffset = [&](int eid, float &outForwardOffset) -> bool {
        for (const auto &pass : growlManifest["draw_passes"]) {
            if (!pass.is_object()) continue;
            if (pass.value("eid", -1) != eid) continue;
            outForwardOffset = pass.value("forward_offset", 0.0f);
            return true;
        }
        return false;
    };

    struct GrowlPairedRingEids {
        int a;
        int b;
    };
    constexpr GrowlPairedRingEids kGrowlPairedRings[] = {
        {1076, 1085},
        {1092, 1101},
        {1108, 1117},
    };

    for (const auto &pair : kGrowlPairedRings) {
        float offsetA = 0.0f;
        float offsetB = 0.0f;
        if (!findGrowlPassForwardOffset(pair.a, offsetA) ||
            !findGrowlPassForwardOffset(pair.b, offsetB)) {
            outFail = "Growl VFX manifest is missing a paired ring pass entry.";
            return false;
        }
        if (std::abs(offsetA - offsetB) > 0.0001f) {
            outFail = "Growl paired ring passes must share the same forward_offset: eid " +
                      std::to_string(pair.a) + " vs eid " + std::to_string(pair.b);
            return false;
        }
    }

    for (const auto &[name, stats] : pokemon.all()) {
        for (const auto &[level, loadout] : stats.loadoutByLevel) {
            bool hasAny = false;
            if (!loadout.fast.empty()) {
                hasAny = true;
                const MoveData *move = moves.getMove(loadout.fast);
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
                const MoveData *move = moves.getMove(loadout.charged);
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
