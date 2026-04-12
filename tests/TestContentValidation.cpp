// tests/TestContentValidation.cpp
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

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

    const std::string growlManifestPath =
        engine::paths::data("config/vfx/moves/growl_draw_passes.json");
    const std::string scratchManifestPath =
        engine::paths::data("config/vfx/moves/scratch_draw_passes.json");
    const std::string scratchShapeOverridesPath =
        engine::paths::data("config/vfx/moves/scratch_shape_overrides.json");
    const std::string scratchGoldGlowTexturePath =
        engine::paths::data("assets/textures/moves/scratch/Texture7567.png");
    const std::string scratchEid1032ClawMeshPath =
        engine::paths::data("assets/meshes/scratch_eid1032_claw_mesh.gltf");
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
    if (!std::filesystem::exists(scratchManifestPath)) {
        outFail = "Missing scratch VFX manifest: " + scratchManifestPath;
        return false;
    }
    if (!std::filesystem::exists(scratchShapeOverridesPath)) {
        outFail = "Missing scratch shape overrides manifest: " + scratchShapeOverridesPath;
        return false;
    }
    if (!std::filesystem::exists(scratchGoldGlowTexturePath)) {
        outFail = "Missing scratch Texture7567 golden glow asset: " + scratchGoldGlowTexturePath;
        return false;
    }
    if (!std::filesystem::exists(scratchEid1032ClawMeshPath)) {
        outFail = "Missing scratch EID 1032 claw mesh asset: " + scratchEid1032ClawMeshPath;
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

    nlohmann::json scratchManifest;
    if (!loadManifest(scratchManifestPath, scratchManifest)) {
        outFail = "Failed to parse scratch VFX manifest: " + scratchManifestPath;
        return false;
    }
    nlohmann::json scratchShapeOverrides;
    if (!loadManifest(scratchShapeOverridesPath, scratchShapeOverrides)) {
        outFail = "Failed to parse scratch shape overrides manifest: " + scratchShapeOverridesPath;
        return false;
    }
    if (!scratchManifest.contains("scratch_sequence") ||
        !scratchManifest["scratch_sequence"].is_object()) {
        outFail = "Scratch VFX manifest must expose the scratch_sequence tuning block.";
        return false;
    }
    const auto &scratchSequence = scratchManifest["scratch_sequence"];
    const int scratchPairCount = scratchSequence.value("pair_count", 0);
    if (scratchPairCount < 1 ||
        scratchPairCount > 5 ||
        !scratchSequence.contains("pair_angle_deg") ||
        !scratchSequence["pair_angle_deg"].is_array() ||
        scratchSequence["pair_angle_deg"].size() < 5u ||
        scratchSequence.value("red_glow_alpha_scale", 1.0f) >= 1.0f ||
        scratchSequence.value("gold_glow_alpha_scale", 0.0f) <= 0.0f ||
        scratchSequence.value("point_glow_enabled", true) ||
        scratchSequence.value("claw_scale_mul", 1.0f) >= 1.0f ||
        scratchSequence.value("claw_width_mul", 1.0f) >= 1.0f ||
        !scratchSequence.value("primary_claw_vsout_shape", false) ||
        scratchSequence.value("angle_jitter_deg", 0.0f) < 0.0f) {
        outFail =
            "Scratch sequence tuning should expose one-to-five timed pairs with softer red glow, no center point pass, and slimmer claw tuning.";
        return false;
    }
    int goldGlowPassCount = 0;
    bool hasEid1032ClawMeshPass = false;
    if (scratchManifest.contains("draw_passes") && scratchManifest["draw_passes"].is_array()) {
        for (const auto &pass : scratchManifest["draw_passes"]) {
            if (pass.is_object() &&
                pass.value("texture", std::string{}) ==
                    "assets/textures/moves/scratch/Texture7567.png") {
                ++goldGlowPassCount;
            }
            if (pass.is_object() &&
                pass.value("mesh", std::string{}) ==
                    "assets/meshes/scratch_eid1032_claw_mesh.gltf") {
                hasEid1032ClawMeshPass = true;
            }
        }
    }
    if (goldGlowPassCount < 5) {
        outFail = "Scratch should pair each red glow with a centered Texture7567 golden glow pass.";
        return false;
    }
    if (!hasEid1032ClawMeshPass) {
        outFail = "Scratch should use the decoded EID 1032 claw mesh for the first claw draw.";
        return false;
    }
    if (!scratchShapeOverrides.contains("primary_claw_vsout") ||
        !scratchShapeOverrides["primary_claw_vsout"].is_object()) {
        outFail = "Scratch shape overrides must expose the primary_claw_vsout shape block.";
        return false;
    }
    const auto &primaryClawVsout = scratchShapeOverrides["primary_claw_vsout"];
    if (primaryClawVsout.value("position_scale", 0.0f) <= 0.0f ||
        !primaryClawVsout.contains("billboards") ||
        !primaryClawVsout["billboards"].is_array() ||
        primaryClawVsout["billboards"].size() < 5u) {
        outFail = "Scratch primary_claw_vsout shape should define a positive position scale and five authored billboard entries.";
        return false;
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
