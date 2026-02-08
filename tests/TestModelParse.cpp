// tests/TestModelParse.cpp
#include <filesystem>
#include <string>

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>

#include "engine/core/Paths.h"
#include "engine/render/FastGLTFLoader.h"
#include "game/config/PokemonConfigLoader.h"

namespace {
bool parseModel(const std::filesystem::path& path, std::string& outFail) {
    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        outFail = "Failed to read model: " + path.string() +
                  " (" + pac::fastgltf_loader::errorName(data.error()) + ")";
        return false;
    }

    fastgltf::Parser parser(pac::fastgltf_loader::kSupportedExtensionsMask);
    constexpr fastgltf::Options kOptions =
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::LoadExternalImages |
        fastgltf::Options::DecomposeNodeMatrices |
        fastgltf::Options::GenerateMeshIndices;
    auto asset = parser.loadGltf(data.get(), path.parent_path(), kOptions, fastgltf::Category::All);

    if (asset.error() != fastgltf::Error::None) {
        outFail = "Failed to parse model: " + path.string() +
                  " (" + pac::fastgltf_loader::errorName(asset.error()) + ")";
        return false;
    }

    return true;
}
} // namespace

bool test_model_parse_smoke(std::string& outFail) {
    PokemonConfigLoader pokemon;
    const std::string configPath = engine::paths::data("config/pokemon_config.json");
    if (!pokemon.loadConfig(configPath, nullptr)) {
        outFail = "Failed to load pokemon config: " + configPath;
        return false;
    }

    int parsed = 0;
    for (const auto& [name, stats] : pokemon.all()) {
        const std::string rel = "models/" + stats.model;
        const std::string modelPath = engine::paths::asset(rel);
        if (!std::filesystem::exists(modelPath)) {
            outFail = "Missing model file for " + name + ": " + modelPath;
            return false;
        }

        if (!parseModel(std::filesystem::path(modelPath), outFail)) {
            return false;
        }
        ++parsed;
    }

    if (parsed == 0) {
        outFail = "No models parsed from pokemon config.";
        return false;
    }

    return true;
}
