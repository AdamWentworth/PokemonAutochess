// tests/TestAnimsetClipNames.cpp
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

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
    if (modelPath.extension() == ".phmodel") {
        nlohmann::json model;
        std::ifstream input(modelPath);
        try {
            input >> model;
        } catch (const std::exception& exception) {
            outFail = "Failed to parse native model IR: " +
                modelPath.string() + " (" + exception.what() + ")";
            return false;
        }
        outNames.clear();
        for (const auto& animation : model.value(
                 "animations", nlohmann::json::array())) {
            const std::string name = animation.value("name", "");
            if (!name.empty()) outNames.insert(name);
        }
        if (outNames.empty()) {
            outFail = "Native model IR has no named animations: " +
                modelPath.string();
            return false;
        }
        return true;
    }

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
} // namespace

bool test_animset_clip_name_smoke(std::string& outFail) {
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

        if (!j.contains("clips") || !j["clips"].is_array()) {
            outFail = "Animset missing clips array: " + animsetPath;
            return false;
        }

        for (const auto& clip : j["clips"]) {
            if (!clip.is_object()) continue;
            std::string clipName = clip.value("gltf_name", "");
            if (clipName.empty()) clipName = clip.value("export_name", "");
            if (clipName.empty()) {
                outFail = "Animset clip missing gltf_name: " + animsetPath;
                return false;
            }
            if (!canResolveClip(animNames, clipName)) {
                outFail = "Animset clip not found in model animations: " + clipName +
                          " (" + animsetPath + ")";
                return false;
            }
        }

        ++checked;
    }

    if (checked == 0) {
        outFail = "No animsets checked from pokemon config.";
        return false;
    }

    return true;
}
