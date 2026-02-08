// tests/TestAnimsetRoles.cpp
#include <algorithm>
#include <cctype>
#include <fstream>
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
                                                  {"buturi", "ba20_buturi", "ba20"}, false);
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
