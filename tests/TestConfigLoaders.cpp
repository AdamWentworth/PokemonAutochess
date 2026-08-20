#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "game/config/EvolutionConfigLoader.h"
#include "game/config/FlyerConfigLoader.h"
#include "game/config/PokemonConfigLoader.h"

namespace {

std::filesystem::path createTempDir(const std::string& label, std::string& outFail) {
    std::error_code ec;
    std::filesystem::path root = std::filesystem::temp_directory_path(ec);
    if (ec) {
        outFail = "temp_directory_path failed: " + ec.message();
        return {};
    }

    const auto stamp = static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count());
    root /= "pac_tests_" + label + "_" + std::to_string(stamp);
    std::filesystem::create_directories(root, ec);
    if (ec) {
        outFail = "create_directories failed: " + ec.message();
        return {};
    }
    return root;
}

void removeTempDir(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

bool writeFile(const std::filesystem::path& path, const std::string& content, std::string& outFail) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        outFail = "Failed to open file for write: " + path.string();
        return false;
    }

    file << content;
    if (!file.good()) {
        outFail = "Failed to write file: " + path.string();
        return false;
    }

    return true;
}

bool nearFloat(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

}  // namespace

bool test_pokemon_config_loader_contract(std::string& outFail) {
    const std::filesystem::path tempDir = createTempDir("pokemon_cfg", outFail);
    if (tempDir.empty()) return false;

    const std::filesystem::path pokemonPath = tempDir / "pokemon.json";
    const std::filesystem::path baseExpPath = tempDir / "pokemon_base_exp.json";

    const std::string pokemonJson = R"({
  "BULBASAUR": {
    "hp": 95,
    "attack": 23,
    "movementSpeed": 1.25,
    "visualScale": 0.01,
    "modelScaleMode": "RAW",
    "modelScaleAxis": "NOT_VALID",
    "model": "BulbasaurCustom.phmodel",
    "modelVariants": {
      "REGULAR": "BulbasaurNative.phmodel",
      "SHINY": "BulbasaurShiny.phmodel"
    },
    "types": ["Grass", "Poison", 7],
    "shopBaseCost": 0,
    "loadoutByLevel": {
      "1": { "fast": "tackle", "charged": "solar_beam" },
      "2": { "fast": "vine_whip" },
      "oops": { "fast": "bad_key" }
    }
  },
  "not_object": 5
})";
    if (!writeFile(pokemonPath, pokemonJson, outFail)) {
        removeTempDir(tempDir);
        return false;
    }

    PokemonConfigLoader loader;
    if (!loader.loadConfig(pokemonPath.string(), nullptr)) {
        outFail = "PokemonConfigLoader::loadConfig failed for valid JSON.";
        removeTempDir(tempDir);
        return false;
    }

    const PokemonStats* stats = loader.getStats("bulbasaur");
    if (!stats) {
        outFail = "Missing species lookup for bulbasaur.";
        removeTempDir(tempDir);
        return false;
    }

    if (!nearFloat(stats->visualScale, 0.05f)) {
        outFail = "visualScale should clamp to 0.05.";
        removeTempDir(tempDir);
        return false;
    }
    if (stats->modelScaleMode != "native") {
        outFail = "modelScaleMode should normalize RAW -> native.";
        removeTempDir(tempDir);
        return false;
    }
    if (stats->modelScaleAxis != "max") {
        outFail = "invalid modelScaleAxis should normalize to max.";
        removeTempDir(tempDir);
        return false;
    }
    if (stats->resolveModel() != "BulbasaurNative.phmodel" ||
        stats->resolveModel("SHINY") != "BulbasaurShiny.phmodel" ||
        stats->resolveModel("female_shiny") != "BulbasaurShiny.phmodel" ||
        stats->resolveModel("female") != "BulbasaurNative.phmodel") {
        outFail = "modelVariants should resolve exact, shiny-compatible, and regular fallback assets.";
        removeTempDir(tempDir);
        return false;
    }

    const std::filesystem::path invalidPokemonPath =
        tempDir / "pokemon_invalid.json";
    for (const std::string& invalidJson : {
             std::string(R"({"missing_model":{"hp":10}})"),
             std::string(R"({"legacy_model":{"model":"Legacy.glb"}})"),
             std::string(R"({"legacy_variant":{"model":"Native.phmodel","modelVariants":{"shiny":"Legacy.glb"}}})")}) {
        if (!writeFile(invalidPokemonPath, invalidJson, outFail)) {
            removeTempDir(tempDir);
            return false;
        }
        if (loader.loadConfig(invalidPokemonPath.string(), nullptr)) {
            outFail =
                "PokemonConfigLoader accepted a missing or non-native Pokemon model identity.";
            removeTempDir(tempDir);
            return false;
        }
        if (!loader.getStats("bulbasaur")) {
            outFail =
                "A rejected Pokemon config should preserve the last valid configuration.";
            removeTempDir(tempDir);
            return false;
        }
    }
    if (stats->shopBaseCost != 1) {
        outFail = "shopBaseCost should clamp to >= 1.";
        removeTempDir(tempDir);
        return false;
    }
    if (stats->types.size() != 2 || stats->types[0] != "grass" || stats->types[1] != "poison") {
        outFail = "types should keep string entries and lowercase them.";
        removeTempDir(tempDir);
        return false;
    }

    auto itLv1 = stats->loadoutByLevel.find(1);
    auto itLv2 = stats->loadoutByLevel.find(2);
    auto itBad = stats->loadoutByLevel.find(0);
    if (itLv1 == stats->loadoutByLevel.end() || itLv2 == stats->loadoutByLevel.end() || itBad != stats->loadoutByLevel.end()) {
        outFail = "loadoutByLevel should keep numeric levels and reject invalid keys.";
        removeTempDir(tempDir);
        return false;
    }
    if (!itLv1->second.hasCharged || itLv1->second.charged != "solar_beam") {
        outFail = "level 1 charged move should parse and set hasCharged.";
        removeTempDir(tempDir);
        return false;
    }
    if (itLv2->second.hasCharged || !itLv2->second.charged.empty()) {
        outFail = "level 2 should have no charged move.";
        removeTempDir(tempDir);
        return false;
    }

    const std::string baseExpJson = R"({
  "Bulbasaur": 321,
  "PIKACHU": 190,
  "broken": "x"
})";
    if (!writeFile(baseExpPath, baseExpJson, outFail)) {
        removeTempDir(tempDir);
        return false;
    }
    if (!loader.applyBaseExpConfig(baseExpPath.string(), nullptr)) {
        outFail = "applyBaseExpConfig failed for valid JSON.";
        removeTempDir(tempDir);
        return false;
    }

    if (loader.getBaseExp("bulbasaur") != 321 || loader.getBaseExp("PIKACHU") != 190) {
        outFail = "base EXP lookup should be case-insensitive.";
        removeTempDir(tempDir);
        return false;
    }
    if (loader.getStats("BULBASAUR")->baseExp != 321) {
        outFail = "base EXP overlay should update loaded species stats.";
        removeTempDir(tempDir);
        return false;
    }

    removeTempDir(tempDir);
    return true;
}

bool test_evolution_flyer_loader_contract(std::string& outFail) {
    const std::filesystem::path tempDir = createTempDir("evo_flyer_cfg", outFail);
    if (tempDir.empty()) return false;

    const std::filesystem::path evolutionPath = tempDir / "evolution.json";
    const std::filesystem::path flyerPath = tempDir / "flyers.json";

    const std::string evolutionJson = R"({
  "CATERPIE": { "evolves_to": "Metapod", "level": 7 },
  "metapod": { "evolves_to": "BUTTERFREE", "level": 10 },
  "invalid_missing": { "evolves_to": "", "level": 3 },
  "invalid_level": { "evolves_to": "x", "level": 0 },
  "invalid_object": 2
})";
    if (!writeFile(evolutionPath, evolutionJson, outFail)) {
        removeTempDir(tempDir);
        return false;
    }

    EvolutionConfigLoader evolution;
    if (!evolution.loadConfig(evolutionPath.string(), nullptr)) {
        outFail = "EvolutionConfigLoader::loadConfig failed for valid JSON.";
        removeTempDir(tempDir);
        return false;
    }
    if (evolution.ruleCount() != 2) {
        outFail = "EvolutionConfigLoader should keep exactly 2 valid rules.";
        removeTempDir(tempDir);
        return false;
    }

    const EvolutionRule* caterpieRule = evolution.getRule("caterpie");
    const EvolutionRule* metapodRule = evolution.getRule("METAPOD");
    if (!caterpieRule || !metapodRule) {
        outFail = "Evolution rules should be case-insensitive.";
        removeTempDir(tempDir);
        return false;
    }
    if (caterpieRule->evolvesTo != "metapod" || caterpieRule->level != 7) {
        outFail = "Unexpected caterpie evolution rule.";
        removeTempDir(tempDir);
        return false;
    }
    if (metapodRule->evolvesTo != "butterfree" || metapodRule->level != 10) {
        outFail = "Unexpected metapod evolution rule.";
        removeTempDir(tempDir);
        return false;
    }

    std::string preEvo;
    if (!evolution.getPreEvolution("BUTTERFREE", preEvo) || preEvo != "metapod") {
        outFail = "getPreEvolution should resolve metapod -> butterfree.";
        removeTempDir(tempDir);
        return false;
    }

    const std::string flyerJson = R"({
  "flyers": ["Pidgey", "SPEAROW", 42],
  "airLocomotionDefaults": {
    "Pidgey": {
      "airLiftY": 0.65,
      "takeoffSec": 0.30,
      "landingSec": 0.42,
      "takeoffAnimSpeed": 1.2,
      "landAnimSpeed": 0.9,
      "debugAnimLogs": true
    },
    "invalid_default": 7
  }
})";
    if (!writeFile(flyerPath, flyerJson, outFail)) {
        removeTempDir(tempDir);
        return false;
    }

    FlyerConfigLoader flyers;
    if (!flyers.loadConfig(flyerPath.string(), nullptr)) {
        outFail = "FlyerConfigLoader::loadConfig failed for valid JSON.";
        removeTempDir(tempDir);
        return false;
    }
    if (flyers.getFlyerCount() != 2 || flyers.getDefaultsCount() != 1) {
        outFail = "FlyerConfigLoader should keep 2 flyers and 1 defaults entry.";
        removeTempDir(tempDir);
        return false;
    }
    if (!flyers.isFlyer("pidgey") || !flyers.isFlyer("Spearow") || flyers.isFlyer("bulbasaur")) {
        outFail = "isFlyer case-insensitive lookup failed.";
        removeTempDir(tempDir);
        return false;
    }

    const auto* defaults = flyers.getAirLocomotionDefaults("PIDGEY");
    if (!defaults) {
        outFail = "Expected air locomotion defaults for pidgey.";
        removeTempDir(tempDir);
        return false;
    }
    if (!defaults->airLiftY.has_value() || !nearFloat(*defaults->airLiftY, 0.65f)) {
        outFail = "airLiftY should parse as 0.65.";
        removeTempDir(tempDir);
        return false;
    }
    if (!defaults->debugAnimLogs.has_value() || !*defaults->debugAnimLogs) {
        outFail = "debugAnimLogs should parse as true.";
        removeTempDir(tempDir);
        return false;
    }
    if (flyers.getAirLocomotionDefaults("SPEAROW") != nullptr) {
        outFail = "Spearow should not have defaults in this fixture.";
        removeTempDir(tempDir);
        return false;
    }

    removeTempDir(tempDir);
    return true;
}
