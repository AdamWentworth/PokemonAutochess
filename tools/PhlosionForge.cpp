#include "engine/assets/lgpe/LgpeCanonicalScene.h"
#include "engine/assets/phlosion/PhlosionResourceContainer.h"
#include "engine/assets/phlosion/PhlosionSceneArchive.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/phlosion/PhlosionModelObject.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr char kPokemonConfig[] = "config/pokemon_config.json";
constexpr char kRoute1Archive[] =
    "content/phlosion/scenes/route1.phscene";
constexpr char kRoute1PrefabRoot[] =
    "content/phlosion/objects/environment/route1";
constexpr char kCookManifest[] =
    "content/phlosion/cook_manifest.json";

std::string hex64(std::uint64_t value) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string out(16u, '0');
    for (std::size_t index = 0u; index < out.size(); ++index) {
        const std::size_t reverseIndex = out.size() - 1u - index;
        out[reverseIndex] = kDigits[value & 0x0full];
        value >>= 4u;
    }
    return out;
}

bool readFile(
    const fs::path& path,
    std::vector<std::uint8_t>& out,
    std::string& outError) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        outError = "Could not open " + path.string();
        return false;
    }
    const std::streamoff length = input.tellg();
    if (length < 0) {
        outError = "Could not measure " + path.string();
        return false;
    }
    out.resize(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!out.empty()) {
        input.read(
            reinterpret_cast<char*>(out.data()),
            static_cast<std::streamsize>(out.size()));
    }
    if (!input) {
        outError = "Could not read " + path.string();
        return false;
    }
    return true;
}

bool writeFile(
    const fs::path& path,
    const std::vector<std::uint8_t>& bytes,
    std::string& outError) {
    std::error_code errorCode;
    fs::create_directories(path.parent_path(), errorCode);
    if (errorCode) {
        outError =
            "Could not create output directory: " +
            errorCode.message();
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        outError = "Could not create " + path.string();
        return false;
    }
    if (!bytes.empty()) {
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        outError = "Could not write " + path.string();
        return false;
    }
    return true;
}

bool loadJson(
    const fs::path& path,
    nlohmann::json& out,
    std::string& outError) {
    std::ifstream input(path);
    if (!input) {
        outError = "Could not open JSON file " + path.string();
        return false;
    }
    try {
        input >> out;
        return true;
    } catch (const std::exception& exception) {
        outError =
            "Could not parse " + path.string() + ": " +
            exception.what();
        return false;
    }
}

bool writeJson(
    const fs::path& path,
    const nlohmann::json& value,
    std::string& outError) {
    const std::string text = value.dump(2) + "\n";
    return writeFile(
        path,
        std::vector<std::uint8_t>(text.begin(), text.end()),
        outError);
}

bool configuredPokemonModels(
    std::vector<std::string>& out,
    std::string& outError) {
    nlohmann::json config;
    if (!loadJson(kPokemonConfig, config, outError)) {
        return false;
    }
    std::set<std::string> unique;
    for (const auto& [pokemonId, record] : config.items()) {
        (void)pokemonId;
        if (!record.contains("model") ||
            !record["model"].is_string()) {
            outError = "Pokemon config contains a missing model field.";
            return false;
        }
        unique.insert(
            (fs::path("assets/models") /
                record["model"].get<std::string>()).generic_string());
    }
    out.assign(unique.begin(), unique.end());
    return !out.empty();
}

bool runtimeAuxiliaryModels(
    std::vector<std::string>& out,
    std::string& outError) {
    std::set<std::string> models{
        "assets/models/pokeball.glb"};
    std::error_code errorCode;
    const fs::path meshRoot("assets/meshes");
    for (fs::recursive_directory_iterator iterator(
             meshRoot,
             errorCode);
         !errorCode && iterator != fs::recursive_directory_iterator();
         iterator.increment(errorCode)) {
        if (iterator->is_regular_file(errorCode) &&
            !errorCode &&
            iterator->path().extension() == ".glb") {
            models.insert(iterator->path().generic_string());
        }
    }
    if (errorCode) {
        outError =
            "Could not enumerate runtime auxiliary models: " +
            errorCode.message();
        return false;
    }
    for (const std::string& path : models) {
        if (!fs::is_regular_file(path, errorCode) || errorCode) {
            outError =
                "Runtime auxiliary model is missing: " + path;
            return false;
        }
    }
    out.assign(models.begin(), models.end());
    return true;
}

bool cookModelSet(
    std::string_view label,
    const std::vector<std::string>& models,
    nlohmann::json& outManifest,
    std::string& outError) {
    outManifest = nlohmann::json::array();
    std::uint64_t totalCookedBytes = 0u;
    std::uint32_t totalTextures = 0u;
    for (std::size_t index = 0u; index < models.size(); ++index) {
        const std::string& modelPath = models[index];
        game::runtime::render_model::MeshData mesh;
        if (!game::runtime::render_model::loadLegacyMeshFromCache(
                modelPath,
                mesh,
                &outError)) {
            outError =
                "Could not decode source model " + modelPath +
                ": " + outError;
            return false;
        }
        game::runtime::phlosion::ModelCookStats stats;
        if (!game::runtime::phlosion::cookModelObject(
                modelPath,
                mesh,
                game::runtime::phlosion::kCookedRoot,
                stats,
                &outError)) {
            outError =
                "Could not cook " + modelPath + ": " + outError;
            return false;
        }
        game::runtime::render_model::MeshData verification;
        const std::string objectPath =
            game::runtime::phlosion::objectPathForModel(modelPath);
        if (!game::runtime::phlosion::loadModelObject(
                objectPath,
                verification,
                &outError)) {
            outError =
                "Could not verify " + objectPath + ": " + outError;
            return false;
        }
        if (verification.vertices.size() != mesh.vertices.size() ||
            verification.indices.size() != mesh.indices.size() ||
            verification.animations.size() != mesh.animations.size() ||
            verification.submeshBaseTextures.size() !=
                mesh.submeshBaseTextures.size()) {
            outError =
                "Round-trip counts changed for " + modelPath;
            return false;
        }
        std::vector<std::uint8_t> sourceBytes;
        std::vector<std::uint8_t> objectBytes;
        if (!readFile(modelPath, sourceBytes, outError) ||
            !readFile(objectPath, objectBytes, outError)) {
            return false;
        }
        totalCookedBytes += stats.cookedBytes;
        totalTextures += stats.textureCount;
        outManifest.push_back({
            {"source", modelPath},
            {"source_fnv1a64",
                hex64(engine::assets::phrc::contentHash64(
                    sourceBytes))},
            {"object", objectPath},
            {"object_fnv1a64",
                hex64(engine::assets::phrc::contentHash64(
                    objectBytes))},
            {"vertices", mesh.vertices.size()},
            {"indices", mesh.indices.size()},
            {"animations", mesh.animations.size()},
            {"ktx2_textures", stats.textureCount},
            {"cooked_bytes", stats.cookedBytes}});
        std::cout
            << "[Phlosion Forge] " << label << " "
            << (index + 1u) << "/" << models.size()
            << ": " << objectPath << "\n";
    }
    std::cout
        << "[Phlosion Forge] Cooked " << models.size()
        << " PHLO prefabs, " << totalTextures
        << " unique KTX2 textures, " << totalCookedBytes
        << " bytes.\n";
    return true;
}

bool cookPokemon(
    nlohmann::json& outManifest,
    std::string& outError) {
    std::vector<std::string> models;
    return configuredPokemonModels(models, outError) &&
        cookModelSet("Pokemon", models, outManifest, outError);
}

bool cookRuntimeAuxiliaries(
    nlohmann::json& outManifest,
    std::string& outError) {
    std::vector<std::string> models;
    return runtimeAuxiliaryModels(models, outError) &&
        cookModelSet(
            "Runtime auxiliary",
            models,
            outManifest,
            outError);
}

bool addVirtualFile(
    const fs::path& path,
    std::map<std::string, engine::assets::phlosion::SceneArchiveFile>& files,
    std::string& outError) {
    const std::string virtualPath = path.generic_string();
    engine::assets::phlosion::SceneArchiveFile file;
    file.virtualPath = virtualPath;
    if (!readFile(path, file.bytes, outError)) {
        return false;
    }
    files[virtualPath] = std::move(file);
    return true;
}

bool addVirtualDirectory(
    const fs::path& directory,
    std::map<std::string, engine::assets::phlosion::SceneArchiveFile>& files,
    std::string& outError) {
    std::error_code errorCode;
    if (!fs::is_directory(directory, errorCode) || errorCode) {
        outError =
            "Route 1 source directory is missing: " +
            directory.string();
        return false;
    }
    for (fs::recursive_directory_iterator iterator(directory, errorCode);
         !errorCode && iterator != fs::recursive_directory_iterator();
         iterator.increment(errorCode)) {
        if (iterator->is_regular_file(errorCode) && !errorCode) {
            if (!addVirtualFile(iterator->path(), files, outError)) {
                return false;
            }
        }
    }
    if (errorCode) {
        outError =
            "Could not enumerate " + directory.string() + ": " +
            errorCode.message();
        return false;
    }
    return true;
}

struct Route1PrefabDefinition {
    const char* id = nullptr;
    const char* fileStem = nullptr;
    const char* displayName = nullptr;
    const char* canonicalRoot = nullptr;
    const char* sourceBoundary = nullptr;
    const char* motionDriver = nullptr;
    std::vector<std::uint32_t> meshIndices;
    nlohmann::json semanticGroups;
};

std::vector<Route1PrefabDefinition> route1PrefabDefinitions() {
    using Definition = Route1PrefabDefinition;
    return {
        Definition{
            "route1/encounter_grass_01",
            "encounter_grass_01",
            "Encounter Grass 01",
            "cache/lgpe/route1_enc_grass01",
            "exact_gamefreak_buildmodel",
            "lgpe_encounter_grass_joint_wind_v1",
            {},
            {{"encounter_grass", nlohmann::json::array({0})}}},
        Definition{
            "route1/encounter_grass_02",
            "encounter_grass_02",
            "Encounter Grass 02",
            "cache/lgpe/route1_enc_grass02",
            "exact_gamefreak_buildmodel",
            "lgpe_encounter_grass_joint_wind_v1",
            {},
            {{"encounter_grass", nlohmann::json::array({0})}}},
        Definition{
            "route1/flowers_02",
            "flowers_02",
            "Flowers 02",
            "cache/lgpe/route1_flowers02",
            "exact_gamefreak_buildmodel",
            "lgpe_vegetation_joint_wind_v1",
            {},
            {{"flowers", nlohmann::json::array({0})}}},
        Definition{
            "route1/flowers_04",
            "flowers_04",
            "Flowers 04",
            "cache/lgpe/route1_flowers04",
            "exact_gamefreak_buildmodel",
            "lgpe_vegetation_joint_wind_v1",
            {},
            {{"flowers", nlohmann::json::array({0})}}},
        Definition{
            "route1/small_grass_02",
            "small_grass_02",
            "Small Grass 02",
            "cache/lgpe/route1_grass02",
            "exact_gamefreak_buildmodel",
            "lgpe_vegetation_joint_wind_v1",
            {},
            {{"small_grass", nlohmann::json::array({0})}}},
        Definition{
            "route1/baked_foliage_collection",
            "baked_foliage_collection",
            "Route 1 Baked Foliage Collection",
            game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            "exact_route_model_mesh_groups_not_individual_source_prefabs",
            "none_source_vertex_programs_are_static",
            {10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u,
             18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u},
            {
                {"tree_groups",
                 nlohmann::json::array(
                     {10u, 11u, 12u, 13u, 14u, 15u})},
                {"ground_foliage_groups",
                 nlohmann::json::array(
                     {16u, 17u, 18u, 19u, 20u,
                      21u, 22u, 23u, 24u, 25u})},
            }},
    };
}

bool cookRoute1Prefabs(
    nlohmann::json& outManifest,
    std::string& outError) {
    outManifest = nlohmann::json::array();
    game::assets::DevAssetStore root(".");
    for (const auto& definition : route1PrefabDefinitions()) {
        std::map<
            std::string,
            engine::assets::phlosion::SceneArchiveFile> files;
        if (!addVirtualDirectory(
                definition.canonicalRoot,
                files,
                outError)) {
            return false;
        }
        std::vector<
            engine::assets::phlosion::SceneArchiveFile> archiveFiles;
        archiveFiles.reserve(files.size());
        for (auto& [path, file] : files) {
            (void)path;
            archiveFiles.push_back(std::move(file));
        }

        const nlohmann::json metadata{
            {"schema_version", 1},
            {"display_name", definition.displayName},
            {"canonical_root", definition.canonicalRoot},
            {"source_boundary", definition.sourceBoundary},
            {"selector",
             {
                 {"mesh_indices", definition.meshIndices},
                 {"semantic_groups", definition.semanticGroups},
             }},
            {"placement",
             {
                 {"owner", "phscene"},
                 {"source_coordinate_system",
                  "source_centimetres_xyz_y_up"},
                 {"preview_origin", "bounds_center_floor"},
             }},
            {"motion",
             {
                 {"driver", definition.motionDriver},
                 {"clock_owner", "phscene"},
                 {"instance_phase_owner", "phscene"},
                 {"period_seconds", 4.0},
             }},
            {"lighting",
             {
                 {"material_response_owner", "phmat_semantics"},
                 {"light_rig_owner", "phscene"},
                 {"required_scene_inputs",
                  nlohmann::json::array({
                      "directional_light",
                      "light_projection",
                      "projected_shadow",
                      "fog",
                  })},
             }},
        };

        std::vector<std::uint8_t> bytes;
        if (!engine::assets::phlosion::encodePrefabArchive(
                definition.id,
                "LgpeEnvironment",
                metadata.dump(),
                std::move(archiveFiles),
                bytes,
                &outError)) {
            return false;
        }
        const fs::path outputPath =
            fs::path(kRoute1PrefabRoot) /
            definition.fileStem /
            (std::string(definition.fileStem) + ".phlo");
        if (!writeFile(outputPath, bytes, outError)) {
            return false;
        }

        engine::assets::phlosion::PrefabArchiveStore prefab;
        if (!prefab.load(
                root,
                outputPath.generic_string(),
                &outError)) {
            return false;
        }
        engine::assets::lgpe::CanonicalScene source;
        if (!engine::assets::lgpe::loadCanonicalScene(
                prefab,
                definition.canonicalRoot,
                source,
                &outError)) {
            outError =
                "Could not validate " +
                std::string(definition.displayName) +
                ": " + outError;
            return false;
        }
        for (const std::uint32_t meshIndex :
             definition.meshIndices) {
            if (meshIndex >= source.meshes.size()) {
                outError =
                    "Route 1 prefab selector is out of range: " +
                    std::string(definition.id);
                return false;
            }
        }

        outManifest.push_back({
            {"asset_id", definition.id},
            {"display_name", definition.displayName},
            {"prefab", outputPath.generic_string()},
            {"prefab_fnv1a64",
             hex64(
                 engine::assets::phrc::contentHash64(bytes))},
            {"cooked_bytes", bytes.size()},
            {"virtual_files", prefab.fileCount()},
            {"source_boundary", definition.sourceBoundary},
            {"motion_driver", definition.motionDriver},
        });
        std::cout
            << "[Phlosion Forge] Route 1 PHLO: "
            << definition.displayName << ", "
            << bytes.size() << " bytes.\n";
    }
    return true;
}

bool cookRoute1(
    nlohmann::json& outManifest,
    std::string& outError) {
    nlohmann::json composition;
    if (!loadJson(
            game::runtime::lgpe_route1_runtime::
                kCompositionManifestPath,
            composition,
            outError)) {
        return false;
    }
    const std::string placementManifest =
        composition.at("buildmodel_vegetation")
            .at("placement_manifest")
            .get<std::string>();
    nlohmann::json placements;
    if (!loadJson(placementManifest, placements, outError)) {
        return false;
    }

    std::set<std::string> directories{
        game::runtime::lgpe_route1_runtime::kCanonicalRoot};
    for (const auto& [logicalName, path] :
         composition.at("encounter_grass").at("models").items()) {
        (void)logicalName;
        directories.insert(path.get<std::string>());
    }
    for (const auto& [logicalName, model] :
         placements.at("models").items()) {
        (void)logicalName;
        directories.insert(model.at("cache_root").get<std::string>());
    }

    std::map<
        std::string,
        engine::assets::phlosion::SceneArchiveFile> files;
    for (const std::string& directory : directories) {
        if (!addVirtualDirectory(directory, files, outError)) {
            return false;
        }
    }
    const std::array<std::string, 3> manifests{
        game::runtime::lgpe_route1_runtime::kCompositionManifestPath,
        game::runtime::lgpe_route1_runtime::kBoardLayoutManifestPath,
        placementManifest};
    for (const std::string& path : manifests) {
        if (!addVirtualFile(path, files, outError)) {
            return false;
        }
    }

    std::vector<engine::assets::phlosion::SceneArchiveFile>
        archiveFiles;
    archiveFiles.reserve(files.size());
    for (auto& [path, file] : files) {
        (void)path;
        archiveFiles.push_back(std::move(file));
    }
    std::vector<std::uint8_t> archiveBytes;
    if (!engine::assets::phlosion::encodeSceneArchive(
            "route1",
            std::move(archiveFiles),
            archiveBytes,
            &outError) ||
        !writeFile(kRoute1Archive, archiveBytes, outError)) {
        return false;
    }

    game::assets::DevAssetStore root(".");
    engine::assets::phlosion::SceneArchiveStore sceneStore;
    if (!sceneStore.load(root, kRoute1Archive, &outError)) {
        return false;
    }
    game::runtime::lgpe_route1_runtime::RuntimeEnvironment environment;
    if (!environment.load(
            sceneStore,
            game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            game::runtime::lgpe_route1_runtime::
                kCompositionManifestPath,
            game::runtime::lgpe_route1_runtime::
                kBoardLayoutManifestPath,
            &outError)) {
        outError =
            "Cooked PHSC Route 1 validation failed: " + outError;
        return false;
    }
    const auto& stats = environment.stats();
    outManifest = {
        {"scene", kRoute1Archive},
        {"scene_fnv1a64",
            hex64(engine::assets::phrc::contentHash64(archiveBytes))},
        {"virtual_files", sceneStore.fileCount()},
        {"cooked_bytes", archiveBytes.size()},
        {"scene_count", stats.sceneCount},
        {"materials", stats.materialCount},
        {"draw_classes", stats.drawClassCount},
        {"visible_triangles", stats.visibleTriangleCount},
        {"shadow_triangles", stats.shadowTriangleCount},
        {"encounter_grass_instances",
            stats.encounterGrassInstanceCount},
        {"vegetation_instances",
            stats.placedVegetationInstanceCount}};
    nlohmann::json prefabs;
    if (!cookRoute1Prefabs(prefabs, outError)) {
        return false;
    }
    outManifest["prefabs"] = std::move(prefabs);
    std::cout
        << "[Phlosion Forge] Route 1 PHSC: "
        << sceneStore.fileCount() << " files, "
        << archiveBytes.size() << " bytes, "
        << stats.visibleTriangleCount << " visible triangles.\n";
    return true;
}

bool validateAll(std::string& outError) {
    std::vector<std::string> models;
    if (!configuredPokemonModels(models, outError)) return false;
    std::vector<std::string> auxiliaries;
    if (!runtimeAuxiliaryModels(auxiliaries, outError)) return false;
    models.insert(
        models.end(),
        auxiliaries.begin(),
        auxiliaries.end());
    for (const std::string& modelPath : models) {
        game::runtime::render_model::MeshData mesh;
        if (!game::runtime::render_model::loadMeshFromCache(
                modelPath,
                mesh,
                &outError)) {
            return false;
        }
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            outError =
                "PHLO contains no renderable geometry: " + modelPath;
            return false;
        }
    }
    game::assets::DevAssetStore root(".");
    engine::assets::phlosion::SceneArchiveStore scene;
    if (!scene.load(root, kRoute1Archive, &outError)) return false;
    game::runtime::lgpe_route1_runtime::RuntimeEnvironment environment;
    if (!environment.load(
            scene,
            game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            game::runtime::lgpe_route1_runtime::
                kCompositionManifestPath,
            game::runtime::lgpe_route1_runtime::
                kBoardLayoutManifestPath,
            &outError)) {
        return false;
    }
    for (const auto& definition : route1PrefabDefinitions()) {
        const fs::path prefabPath =
            fs::path(kRoute1PrefabRoot) /
            definition.fileStem /
            (std::string(definition.fileStem) + ".phlo");
        engine::assets::phlosion::PrefabArchiveStore prefab;
        if (!prefab.load(
                root,
                prefabPath.generic_string(),
                &outError) ||
            prefab.prefabId() != definition.id ||
            prefab.prefabKind() != "LgpeEnvironment") {
            outError =
                "Route 1 environment PHLO validation failed for " +
                std::string(definition.displayName) + ": " +
                outError;
            return false;
        }
        engine::assets::lgpe::CanonicalScene source;
        if (!engine::assets::lgpe::loadCanonicalScene(
                prefab,
                definition.canonicalRoot,
                source,
                &outError)) {
            return false;
        }
    }
    std::cout
        << "[Phlosion Forge] Strict validation passed for "
        << models.size()
        << " gameplay PHLO prefabs, "
        << route1PrefabDefinitions().size()
        << " Route 1 environment PHLO prefabs, and Route 1 PHSC.\n";
    return true;
}

void usage() {
    std::cerr
        << "Usage: PhlosionForge "
        << "<cook-all|cook-pokemon|cook-runtime|cook-route1|validate>\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        usage();
        return 2;
    }
    const std::string command = argv[1];
    std::string error;
    nlohmann::json pokemon;
    nlohmann::json runtimeAuxiliaries;
    nlohmann::json route1;
    if ((command == "cook-all" || command == "cook-pokemon") &&
        !cookPokemon(pokemon, error)) {
        std::cerr << "[Phlosion Forge] ERROR: " << error << "\n";
        return 1;
    }
    if ((command == "cook-all" || command == "cook-runtime") &&
        !cookRuntimeAuxiliaries(runtimeAuxiliaries, error)) {
        std::cerr << "[Phlosion Forge] ERROR: " << error << "\n";
        return 1;
    }
    if ((command == "cook-all" || command == "cook-route1") &&
        !cookRoute1(route1, error)) {
        std::cerr << "[Phlosion Forge] ERROR: " << error << "\n";
        return 1;
    }
    if (command == "cook-all") {
        const nlohmann::json manifest{
            {"schema_version", 1},
            {"kind", "phlosion_cook_manifest"},
            {"environment", route1},
            {"pokemon", pokemon},
            {"runtime_auxiliary_objects", runtimeAuxiliaries}};
        if (!writeJson(kCookManifest, manifest, error)) {
            std::cerr
                << "[Phlosion Forge] ERROR: " << error << "\n";
            return 1;
        }
        if (!validateAll(error)) {
            std::cerr
                << "[Phlosion Forge] ERROR: " << error << "\n";
            return 1;
        }
        std::cout
            << "[Phlosion Forge] Wrote " << kCookManifest << "\n";
        return 0;
    }
    if (command == "cook-pokemon" ||
        command == "cook-runtime" ||
        command == "cook-route1") {
        return 0;
    }
    if (command == "validate") {
        if (!validateAll(error)) {
            std::cerr
                << "[Phlosion Forge] ERROR: " << error << "\n";
            return 1;
        }
        return 0;
    }
    usage();
    return 2;
}
