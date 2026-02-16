// tests/TestGltfAsset.cpp
#include <filesystem>
#include <string>

#include "engine/core/Paths.h"
#include "engine/render/gltf/FastGLTFLoader.h"
#include "game/config/PokemonConfigLoader.h"

bool test_gltf_asset_smoke(std::string& outFail) {
    PokemonConfigLoader pokemon;
    const std::string configPath = engine::paths::data("config/pokemon_config.json");
    if (!pokemon.loadConfig(configPath, nullptr)) {
        outFail = "Failed to load pokemon config: " + configPath;
        return false;
    }

    std::string modelPath;
    for (const auto& [name, stats] : pokemon.all()) {
        modelPath = engine::paths::asset("models/" + stats.model);
        break;
    }

    if (modelPath.empty()) {
        outFail = "No model paths found in pokemon config.";
        return false;
    }
    if (!std::filesystem::exists(modelPath)) {
        outFail = "Missing model file: " + modelPath;
        return false;
    }

    auto result = pac::fastgltf_loader::tryLoad(modelPath);
    if (!result.has_value()) {
        outFail = "Failed to parse model asset: " + modelPath;
        return false;
    }

    const auto& asset = result->asset;
    if (asset.nodes.empty()) {
        outFail = "Parsed asset has no nodes: " + modelPath;
        return false;
    }
    if (asset.meshes.empty()) {
        outFail = "Parsed asset has no meshes: " + modelPath;
        return false;
    }
    if (asset.animations.empty()) {
        outFail = "Parsed asset has no animations: " + modelPath;
        return false;
    }

    return true;
}
