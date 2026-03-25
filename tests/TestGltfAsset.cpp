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

    const auto& allPokemon = pokemon.all();
    const auto firstPokemon = allPokemon.begin();
    if (firstPokemon == allPokemon.end()) {
        outFail = "No model paths found in pokemon config.";
        return false;
    }
    const std::string modelPath = engine::paths::asset("models/" + firstPokemon->second.model);
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

    const std::string backdropTreeModelPath =
        engine::paths::asset("models/environment/route_evergreen_tree.glb");
    if (!std::filesystem::exists(backdropTreeModelPath)) {
        outFail = "Missing backdrop tree model file: " + backdropTreeModelPath;
        return false;
    }

    auto backdropTree = pac::fastgltf_loader::tryLoad(backdropTreeModelPath);
    if (!backdropTree.has_value()) {
        outFail = "Failed to parse backdrop tree asset: " + backdropTreeModelPath;
        return false;
    }
    if (backdropTree->asset.meshes.empty()) {
        outFail = "Backdrop tree asset has no meshes: " + backdropTreeModelPath;
        return false;
    }
    if (backdropTree->asset.materials.empty()) {
        outFail = "Backdrop tree asset has no materials: " + backdropTreeModelPath;
        return false;
    }
    if (backdropTree->asset.textures.empty()) {
        outFail = "Backdrop tree asset has no textures: " + backdropTreeModelPath;
        return false;
    }

    return true;
}
