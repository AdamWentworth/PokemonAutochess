// tests/TestContentValidation.cpp
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "engine/core/Paths.h"
#include "game/config/PokemonConfigLoader.h"
#include "game/config/MovesConfigLoader.h"
#include "game/runtime/phlosion/PhlosionModelObject.h"

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

    for (const auto& [species, stats] : pokemon.all()) {
        const auto validateCookedModel =
            [&](const std::string& model) {
                if (std::filesystem::path(model).extension() !=
                    ".phmodel") {
                    outFail =
                        "Pokemon configuration contains a runtime interchange/cache model for " +
                        species + ": " + model;
                    return false;
                }
                const std::string configuredIdentity =
                    "assets/models/" + model;
                const std::string cookedObject =
                    game::runtime::phlosion::objectPathForModel(
                        configuredIdentity);
                if (!std::filesystem::exists(cookedObject)) {
                    outFail =
                        "Pokemon configuration has no cooked PHLO object for " +
                        species + ": " + cookedObject;
                    return false;
                }
                return true;
            };
        if (!validateCookedModel(stats.model)) return false;
        for (const auto& [variant, model] : stats.modelVariants) {
            (void)variant;
            if (!validateCookedModel(model)) return false;
        }
    }

    const std::set<std::string> approvedRuntimeGlbIdentities{
        "assets/models/pokeball.glb",
        "assets/models/environment/route_evergreen_tree.glb",
        "assets/models/environment/route1.glb",
        "assets/meshes/growl_1076_mesh.glb",
        "assets/meshes/growl_1085_mesh.glb",
        "assets/meshes/growl_1092_mesh.glb",
        "assets/meshes/growl_1101_mesh.glb",
        "assets/meshes/growl_1108_mesh.glb",
        "assets/meshes/growl_1117_mesh.glb",
        "assets/meshes/growl_1128_mesh.glb",
        "assets/meshes/growl_1255_mesh.glb",
        "assets/meshes/growl_1275_mesh.glb",
    };
    const std::regex runtimeGlbIdentity(
        R"(assets/(?:models|meshes)/[A-Za-z0-9_./-]+\.glb)");
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(
             "src/game")) {
        if (!entry.is_regular_file()) continue;
        const std::string extension =
            entry.path().extension().string();
        if (extension != ".cpp" && extension != ".h") continue;
        std::ifstream source(entry.path(), std::ios::binary);
        const std::string sourceText{
            std::istreambuf_iterator<char>(source),
            std::istreambuf_iterator<char>()};
        for (auto match = std::sregex_iterator(
                 sourceText.begin(),
                 sourceText.end(),
                 runtimeGlbIdentity);
             match != std::sregex_iterator();
             ++match) {
            const std::string identity = match->str();
            if (!approvedRuntimeGlbIdentities.contains(identity)) {
                outFail =
                    "Runtime source names an unapproved GLB identity: " +
                    identity + " (" + entry.path().generic_string() + ")";
                return false;
            }
        }
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
        const std::string sourcePrefix = std::string(species) == "pidgey"
            ? "pm0016_00_00"
            : "pm0017_00_00";
        for (const std::string& variantSuffix : {std::string{}, std::string{"_Shiny"}}) {
            const std::string animsetPath = engine::paths::data(
                "assets/models/" + std::string(stem) +
                variantSuffix + ".animset.json");
            std::ifstream input(animsetPath);
            nlohmann::json animset;
            try {
                if (!input.is_open()) {
                    outFail = "Missing Pidgey-family animset: " + animsetPath;
                    return false;
                }
                input >> animset;
            } catch (...) {
                outFail = "Could not parse Pidgey-family animset: " + animsetPath;
                return false;
            }
            const auto& roles = animset["roles"];
            const auto& meta = animset["meta"];
            if (!roles.is_object() || !meta.is_object() ||
                roles.value("move", "") != sourcePrefix + "_20030_walk01_loop" ||
                roles.value("move_fast", "") != sourcePrefix + "_20100_run01_loop" ||
                roles.value("air_idle", "") != sourcePrefix + "_20000_defaultwait01_loop" ||
                roles.value("takeoff", "") != sourcePrefix + "_00150_jumpup01_start" ||
                roles.value("takeoff_loop", "") != sourcePrefix + "_00151_jumpup01_loop" ||
                roles.value("land_a", "") != sourcePrefix + "_00152_jumpdown01_start" ||
                roles.value("land_b", "") != sourcePrefix + "_00153_jumpdown01_loop" ||
                roles.value("land_c", "") != sourcePrefix + "_00155_land02" ||
                meta.value("movementMode", "") != "airborne") {
                outFail =
                    "Pidgey-family native animset must preserve the Z-A ground-to-air movement sequence: " +
                    animsetPath;
                return false;
            }
        }
    }

    const PokemonStats* pikachu = pokemon.getStats("pikachu");
    if (!pikachu ||
        pikachu->model != "0025_Pikachu_SV.phmodel" ||
        pikachu->resolveModel("regular") !=
            "0025_Pikachu_SV.phmodel" ||
        pikachu->resolveModel("shiny") !=
            "0025_Pikachu_SV_Shiny.phmodel" ||
        pikachu->resolveModel("female") !=
            "0025_Pikachu_SV_Female.phmodel" ||
        pikachu->resolveModel("female_shiny") !=
            "0025_Pikachu_SV_Female_Shiny.phmodel") {
        outFail =
            "Pikachu config must select native Scarlet/Violet male/female regular and shiny models.";
        return false;
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

    const PokemonStats* mankey = pokemon.getStats("mankey");
    if (!mankey ||
        mankey->model != "0056_Mankey_SV.phmodel" ||
        mankey->resolveModel("regular") !=
            "0056_Mankey_SV.phmodel" ||
        mankey->resolveModel("shiny") !=
            "0056_Mankey_SV_Shiny.phmodel") {
        outFail =
            "Mankey config must select native Scarlet/Violet regular and shiny models.";
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
