#include <algorithm>
#include <filesystem>
#include <string>

#include "engine/core/Paths.h"
#include "game/config/PokemonConfigLoader.h"
#include "game/runtime/phlosion/PhlosionModelObject.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"

bool test_model_asset_smoke(std::string& outFail) {
    PokemonConfigLoader pokemon;
    const std::string configPath = engine::paths::data("config/pokemon_config.json");
    if (!pokemon.loadConfig(configPath, nullptr)) {
        outFail = "Failed to load pokemon config: " + configPath;
        return false;
    }

    const auto& allPokemon = pokemon.all();
    const auto firstModelPokemon = std::find_if(
        allPokemon.begin(),
        allPokemon.end(),
        [](const auto& entry) {
            const std::filesystem::path model(entry.second.model);
            const std::string extension = model.extension().string();
            return extension == ".glb" || extension == ".gltf" ||
                   extension == ".phmodel";
        });
    if (firstModelPokemon == allPokemon.end()) {
        outFail = "No supported model paths found in pokemon config.";
        return false;
    }
    const std::string modelPath = engine::paths::asset(
        "models/" + firstModelPokemon->second.model);
    if (!std::filesystem::exists(modelPath)) {
        outFail = "Missing model file: " + modelPath;
        return false;
    }
    const std::string configuredIdentity =
        "assets/models/" + firstModelPokemon->second.model;
    if (game::runtime::phlosion::objectPathForModel(modelPath) !=
        game::runtime::phlosion::objectPathForModel(configuredIdentity)) {
        outFail =
            "Absolute and configured model paths produced different PHLO "
            "identities: " + modelPath;
        return false;
    }

    game::runtime::render_model::MeshData mesh;
    std::string loadError;
    if (!game::runtime::render_model::loadMeshFromCache(
            modelPath,
            mesh,
            &loadError)) {
        outFail =
            "Failed to load configured model asset: " + modelPath +
            " :: " + loadError;
        return false;
    }

    if (mesh.nodeNames.empty()) {
        outFail = "Loaded asset has no nodes: " + modelPath;
        return false;
    }
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        outFail = "Loaded asset has no mesh geometry: " + modelPath;
        return false;
    }
    if (mesh.animations.empty()) {
        outFail = "Loaded asset has no animations: " + modelPath;
        return false;
    }

    return true;
}
