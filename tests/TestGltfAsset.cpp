// tests/TestGltfAsset.cpp
#include <filesystem>
#include <algorithm>
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

    const auto& allPokemon = pokemon.all();
    const auto firstGltfPokemon = std::find_if(
        allPokemon.begin(),
        allPokemon.end(),
        [](const auto& entry) {
            const std::filesystem::path model(entry.second.model);
            const std::string extension = model.extension().string();
            return extension == ".glb" || extension == ".gltf";
        });
    if (firstGltfPokemon == allPokemon.end()) {
        outFail = "No glTF model paths found in pokemon config.";
        return false;
    }
    const std::string modelPath = engine::paths::asset(
        "models/" + firstGltfPokemon->second.model);
    if (!std::filesystem::exists(modelPath)) {
        outFail = "Missing model file: " + modelPath;
        return false;
    }

    auto result = engine::render::gltf::loader::tryLoad(modelPath);
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
