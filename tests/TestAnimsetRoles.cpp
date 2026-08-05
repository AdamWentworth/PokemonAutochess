// tests/TestAnimsetRoles.cpp
#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_set>
#include <string>

#include <nlohmann/json.hpp>

#include "engine/core/Paths.h"
#include "game/config/AnimSetLoader.h"
#include "game/config/PokemonConfigLoader.h"

namespace {
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string stripSuffix(const std::string& s, const std::string& suffix) {
    if (s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix) {
        return s.substr(0, s.size() - suffix.size());
    }
    return s;
}

bool clipExists(const nlohmann::json& j, const std::string& name) {
    if (!j.contains("clips") || !j["clips"].is_array()) return false;
    const std::string wantedLower = toLower(name);
    const std::string wantedStripped = stripSuffix(wantedLower, ".gfbanm");

    for (const auto& clip : j["clips"]) {
        if (!clip.is_object()) continue;
        std::string clipName = clip.value("gltf_name", "");
        if (clipName.empty()) clipName = clip.value("export_name", "");
        if (clipName.empty()) continue;

        if (clipName == name) return true;

        const std::string candLower = toLower(clipName);
        if (candLower == wantedLower) return true;

        const std::string candStripped = stripSuffix(candLower, ".gfbanm");
        if (candStripped == wantedStripped) return true;
    }
    return false;
}

bool readJson(const std::string& path, nlohmann::json& out, std::string& outFail) {
    std::ifstream f(path);
    if (!f) {
        outFail = "Failed to open animset: " + path;
        return false;
    }
    try {
        f >> out;
    } catch (...) {
        outFail = "Failed to parse animset JSON: " + path;
        return false;
    }
    return true;
}
} // namespace

bool test_animset_roles_smoke(std::string& outFail) {
    PokemonConfigLoader pokemon;
    const std::string configPath = engine::paths::data("config/pokemon_config.json");
    if (!pokemon.loadConfig(configPath, nullptr)) {
        outFail = "Failed to load pokemon config: " + configPath;
        return false;
    }

    int checked = 0;
    for (const auto& [name, stats] : pokemon.all()) {
        const std::string modelPath = engine::paths::asset("models/" + stats.model);
        const std::string animsetPath = AnimSet::animSetPathFromModelPath(modelPath);

        nlohmann::json j;
        if (!readJson(animsetPath, j, outFail)) return false;

        const auto idlePick = AnimSet::resolveRoleClip(j, "idle", "idle",
                                                       {"battlewait", "defaultwait", "idle", "wait"}, true);
        if (!idlePick.valid || idlePick.clipName.empty()) {
            outFail = "Animset missing idle role: " + animsetPath;
            return false;
        }
        if (!clipExists(j, idlePick.clipName)) {
            outFail = "Animset idle clip not found in clips: " + animsetPath;
            return false;
        }

        const auto movePick = AnimSet::resolveRoleClip(j, "move", "move",
                                                       {"run", "dash", "move"}, true);
        if (!movePick.valid || movePick.clipName.empty()) {
            outFail = "Animset missing move role: " + animsetPath;
            return false;
        }
        if (!clipExists(j, movePick.clipName)) {
            outFail = "Animset move clip not found in clips: " + animsetPath;
            return false;
        }

        auto attackPick = AnimSet::resolveRoleClip(j, "attack1", "attack",
                                                   {"attack01", "attack1", "attack"}, true);
        if (!attackPick.valid || attackPick.clipName.empty()) {
            attackPick = AnimSet::resolveRoleClip(j, "attack1", "misc",
                                                  {"buturi", "ba20_buturi", "ba20", "tokusyu", "ba21"}, false);
        }
        if (!attackPick.valid || attackPick.clipName.empty()) {
            outFail = "Animset missing attack role: " + animsetPath;
            return false;
        }
        if (!clipExists(j, attackPick.clipName)) {
            outFail = "Animset attack clip not found in clips: " + animsetPath;
            return false;
        }

        ++checked;
    }

    if (checked == 0) {
        outFail = "No animsets checked from pokemon config.";
        return false;
    }

    return true;
}

bool test_animset_roles_prefer_best_idle_match(std::string& outFail) {
    struct RoleExpectation {
        std::string animsetPath;
        std::string roleKey;
        std::string fallbackCategory;
        std::vector<std::string> preferredSubstrings;
        std::string expectedClip;
    };

    const std::vector<RoleExpectation> expectations = {
        {
            engine::paths::asset("models/0025_Pikachu.animset.json"),
            "idle",
            "idle",
            {"battlewait", "defaultwait", "kw01_wait", "idle", "wait"},
            "pm0025_00_00_00000_defaultwait01_loop.gfbanm",
        },
        {
            engine::paths::asset("models/0021_Spearow.animset.json"),
            "ground_idle",
            "idle",
            {"ba10_wait", "battlewait", "ba10", "wait", "idle"},
            "pm0021_00_ba10_waitA01.gfbanm",
        },
        {
            engine::paths::asset("models/0021_Spearow.animset.json"),
            "air_idle",
            "idle",
            {"fi01_wait", "fly", "air", "hover"},
            "pm0021_00_fi01_wait01.gfbanm",
        },
        {
            engine::paths::asset("models/0056_Mankey.animset.json"),
            "idle",
            "idle",
            {"battlewait", "defaultwait", "kw01_wait", "idle", "wait"},
            "pm0056_00_00_00001_battlewait01_loop.gfbanm",
        },
        {
            engine::paths::asset("models/0014_Kakuna_ZA.animset.json"),
            "idle",
            "idle",
            {"battlewait", "defaultwait", "kw01_wait", "idle", "wait"},
            "pm0014_00_00_00001_battlewait01_loop",
        },
    };

    for (const auto& expectation : expectations) {
        nlohmann::json j;
        if (!readJson(expectation.animsetPath, j, outFail)) return false;

        const auto pick = AnimSet::resolveRoleClip(
            j,
            expectation.roleKey,
            expectation.fallbackCategory,
            expectation.preferredSubstrings,
            true);
        if (!pick.valid || pick.clipName.empty()) {
            outFail = "Expected a valid role pick for animset: " + expectation.animsetPath;
            return false;
        }
        if (pick.clipName != expectation.expectedClip) {
            outFail =
                "Unexpected clip for role '" + expectation.roleKey + "' in " +
                expectation.animsetPath +
                ": expected '" + expectation.expectedClip +
                "', got '" + pick.clipName + "'";
            return false;
        }
    }

    const auto optionalRoleJson = nlohmann::json::parse(R"({
  "clips": [
    { "gltf_name": "pm_ground_ba01_landA01.gfbanm", "category": "misc", "duration_seconds": 0.64 },
    { "gltf_name": "pm_ground_ba01_landB01.gfbanm", "category": "misc", "duration_seconds": 0.44 },
    { "gltf_name": "pm_ground_ba01_landC01.gfbanm", "category": "misc", "duration_seconds": 1.16 },
    { "gltf_name": "pm_ground_ba20_buturi01.gfbanm", "category": "misc", "duration_seconds": 2.32 }
  ]
})");

    const auto takeoffPick = AnimSet::resolveRoleClip(
        optionalRoleJson,
        "takeoff",
        "misc",
        {"take_flight", "takeflight", "takeoff"},
        false);
    if (takeoffPick.valid) {
        outFail = "Optional takeoff role should not fall back to the first misc clip when no takeoff-like name exists.";
        return false;
    }

    const auto landAPick = AnimSet::resolveRoleClip(
        optionalRoleJson,
        "land_a",
        "misc",
        {"landa"},
        false);
    if (!landAPick.valid || landAPick.clipName != "pm_ground_ba01_landA01.gfbanm") {
        outFail = "land_a role should still resolve from preferred substrings inside misc clips.";
        return false;
    }

    const auto miscAttackPick = AnimSet::resolveRoleClip(
        optionalRoleJson,
        "attack1",
        "misc",
        {"buturi", "ba20_buturi", "ba20"},
        false);
    if (!miscAttackPick.valid || miscAttackPick.clipName != "pm_ground_ba20_buturi01.gfbanm") {
        outFail = "attack1 misc fallback should continue to resolve when an attack-like misc clip exists.";
        return false;
    }

    return true;
}

bool test_native_starter_motion_sets_preserved(std::string& outFail) {
    struct Expectation {
        const char* stem;
        const char* alternateSet;
        const char* alternatePrefix;
        int expectedClipCount;
    };
    constexpr Expectation expectations[] = {
        {"0006_Charizard_SV", "aerial", "_200", 83},
        {"0007_Squirtle_SV", "aquatic", "_100", 86},
        {"0008_Wartortle_SV", "aquatic", "_100", 83},
        {"0009_Blastoise_SV", "aquatic", "_100", 86},
    };

    for (const auto& expectation : expectations) {
        const std::string path = engine::paths::asset(
            "models/" + std::string(expectation.stem) + ".animset.json");
        nlohmann::json j;
        if (!readJson(path, j, outFail)) return false;
        if (j.value("schema", "") != "animset-v3" ||
            j.value("default_motion_set", "") != "ground") {
            outFail = "Native starter animset must use animset-v3 with ground as its default motion set: " + path;
            return false;
        }
        if (!j.contains("clips") || !j["clips"].is_array() ||
            static_cast<int>(j["clips"].size()) != expectation.expectedClipCount) {
            outFail = "Native starter animset lost source animation clips: " + path;
            return false;
        }

        std::unordered_set<std::string> clipKeys;
        for (const auto& clip : j["clips"]) {
            const std::string key = clip.value("clip_key", "");
            if (key.empty() || !clipKeys.insert(key).second) {
                outFail = "Native starter animset clip keys must be non-empty and unique: " + path;
                return false;
            }
        }
        if (!j.contains("by_key") || !j["by_key"].is_object() ||
            j["by_key"].size() != j["clips"].size()) {
            outFail = "Native starter animset by_key index must retain every physical clip: " + path;
            return false;
        }
        if (!j.contains("role_sets") || !j["role_sets"].is_object() ||
            !j["role_sets"].contains("ground") ||
            !j["role_sets"].contains(expectation.alternateSet)) {
            outFail = "Native starter animset is missing a source locomotion family: " + path;
            return false;
        }

        const std::string groundIdle =
            j["role_sets"]["ground"].value("idle", "");
        const std::string alternateIdle =
            j["role_sets"][expectation.alternateSet].value("idle", "");
        if (groundIdle.find("_000") == std::string::npos ||
            alternateIdle.find(expectation.alternatePrefix) == std::string::npos ||
            j["roles"].value("idle", "") != groundIdle ||
            !clipExists(j, groundIdle) ||
            !clipExists(j, alternateIdle)) {
            outFail = "Native starter ground and alternate locomotion roles are not isolated correctly: " + path;
            return false;
        }
    }

    return true;
}
