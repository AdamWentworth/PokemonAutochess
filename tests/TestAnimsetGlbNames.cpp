// tests/TestAnimsetGlbNames.cpp
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <nlohmann/json.hpp>

#include "engine/core/Paths.h"
#include "engine/render/gltf/FastGLTFLoader.h"
#include "game/config/AnimSetLoader.h"
#include "game/config/PokemonConfigLoader.h"

namespace {
std::string stripSuffix(const std::string& s, const std::string& suffix) {
    if (s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix) {
        return s.substr(0, s.size() - suffix.size());
    }
    return s;
}

bool canResolveClip(const std::unordered_set<std::string>& names, const std::string& clipName) {
    if (names.count(clipName)) return true;

    std::string cand = stripSuffix(clipName, ".gfbanm");
    if (names.count(cand)) return true;

    cand = stripSuffix(clipName, "__START");
    if (names.count(cand)) return true;

    cand = stripSuffix(clipName, "__END");
    if (names.count(cand)) return true;

    std::string tmp = stripSuffix(clipName, ".gfbanm");
    tmp = stripSuffix(tmp, "__START");
    tmp = stripSuffix(tmp, "__END");
    if (names.count(tmp)) return true;

    return false;
}

bool loadAnimsetJson(const std::string& animsetPath, nlohmann::json& outJson, std::string& outFail) {
    if (!AnimSet::loadAnimSetJson(animsetPath, outJson)) {
        outFail = "Failed to parse animset: " + animsetPath;
        return false;
    }
    return true;
}

bool parseAnimationNames(const std::filesystem::path& modelPath,
                         std::unordered_set<std::string>& outNames,
                         std::string& outFail) {
    auto data = fastgltf::GltfDataBuffer::FromPath(modelPath);
    if (data.error() != fastgltf::Error::None) {
        outFail = "Failed to read model: " + modelPath.string() +
                  " (" + std::string(engine::render::gltf::loader::errorName(data.error())) + ")";
        return false;
    }

    fastgltf::Parser parser(engine::render::gltf::loader::kSupportedExtensionsMask);
    constexpr fastgltf::Options kOptions =
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::LoadExternalImages |
        fastgltf::Options::DecomposeNodeMatrices |
        fastgltf::Options::GenerateMeshIndices;

    auto asset = parser.loadGltf(data.get(), modelPath.parent_path(), kOptions, fastgltf::Category::All);
    if (asset.error() != fastgltf::Error::None) {
        outFail = "Failed to parse model: " + modelPath.string() +
                  " (" + std::string(engine::render::gltf::loader::errorName(asset.error())) + ")";
        return false;
    }

    outNames.clear();
    for (const auto& anim : asset.get().animations) {
        if (!anim.name.empty()) {
            outNames.insert(std::string(anim.name));
        }
    }

    if (outNames.empty()) {
        outFail = "Model has no named animations: " + modelPath.string();
        return false;
    }

    return true;
}

bool checkRoleClip(const std::string& role,
                   const AnimSet::RolePick& pick,
                   const std::unordered_set<std::string>& names,
                   const std::string& animsetPath,
                   std::string& outFail,
                   bool required) {
    if (!pick.valid || pick.clipName.empty()) {
        if (required) {
            outFail = "Animset missing role '" + role + "': " + animsetPath;
            return false;
        }
        return true;
    }

    if (!canResolveClip(names, pick.clipName)) {
        outFail = "Animset role '" + role + "' clip not found in GLB animations: " +
                  pick.clipName + " (" + animsetPath + ")";
        return false;
    }

    return true;
}
} // namespace

bool test_animset_glb_name_smoke(std::string& outFail) {
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
        if (!loadAnimsetJson(animsetPath, j, outFail)) return false;

        std::unordered_set<std::string> animNames;
        if (!parseAnimationNames(std::filesystem::path(modelPath), animNames, outFail)) return false;

        const auto idlePick = AnimSet::resolveRoleClip(j, "idle", "idle",
                                                       {"battlewait", "defaultwait", "idle", "wait"}, true);
        if (!checkRoleClip("idle", idlePick, animNames, animsetPath, outFail, true)) return false;

        const auto movePick = AnimSet::resolveRoleClip(j, "move", "move",
                                                       {"run", "dash", "move"}, true);
        if (!checkRoleClip("move", movePick, animNames, animsetPath, outFail, true)) return false;

        auto attackPick = AnimSet::resolveRoleClip(j, "attack1", "attack",
                                                   {"attack01", "attack1", "attack"}, true);
        if (!attackPick.valid || attackPick.clipName.empty()) {
            attackPick = AnimSet::resolveRoleClip(j, "attack1", "misc",
                                                  {"buturi", "ba20_buturi", "ba20", "tokusyu", "ba21"}, false);
        }
        if (!checkRoleClip("attack1", attackPick, animNames, animsetPath, outFail, true)) return false;

        const auto groundIdlePick = AnimSet::resolveRoleClip(j, "ground_idle", "idle",
                                                             {"ba10_wait", "battlewait", "ba10", "wait", "idle"}, true);
        if (!checkRoleClip("ground_idle", groundIdlePick, animNames, animsetPath, outFail, false)) return false;

        const auto airIdlePick = AnimSet::resolveRoleClip(j, "air_idle", "idle",
                                                          {"fi01_wait", "fly", "air", "hover"}, true);
        if (!checkRoleClip("air_idle", airIdlePick, animNames, animsetPath, outFail, false)) return false;

        const auto takeoffPick = AnimSet::resolveRoleClip(j, "takeoff", "misc",
                                                          {"take_flight", "takeflight", "takeoff"}, false);
        if (!checkRoleClip("takeoff", takeoffPick, animNames, animsetPath, outFail, false)) return false;

        const auto landAPick = AnimSet::resolveRoleClip(j, "land_a", "misc", {"landa"}, false);
        if (!checkRoleClip("land_a", landAPick, animNames, animsetPath, outFail, false)) return false;

        const auto landBPick = AnimSet::resolveRoleClip(j, "land_b", "misc", {"landb"}, false);
        if (!checkRoleClip("land_b", landBPick, animNames, animsetPath, outFail, false)) return false;

        const auto landCPick = AnimSet::resolveRoleClip(j, "land_c", "misc", {"landc"}, false);
        if (!checkRoleClip("land_c", landCPick, animNames, animsetPath, outFail, false)) return false;

        const auto landPick = AnimSet::resolveRoleClip(j, "land", "misc", {"land"}, false);
        if (!checkRoleClip("land", landPick, animNames, animsetPath, outFail, false)) return false;

        ++checked;
    }

    if (checked == 0) {
        outFail = "No animsets checked from pokemon config.";
        return false;
    }

    return true;
}
