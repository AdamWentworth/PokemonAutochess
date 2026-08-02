#include "engine/assets/lgpe/LgpeCanonicalScene.h"
#include "engine/assets/phlosion/PhlosionResourceContainer.h"
#include "engine/assets/phlosion/PhlosionSceneArchive.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/phlosion/PhlosionModelObject.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"
#include "game/runtime/shared/scene/LgpeRoute1TerrainAssemblies.h"
#include "game/runtime/shared/scene/LgpeRoute1TreeInstances.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
constexpr char kRoute1AuthoredScene[] =
    "scenes/route1.scene.json";
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
    std::string_view prefabKind,
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
                prefabKind,
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
        cookModelSet(
            "Pokemon",
            "Character",
            models,
            outManifest,
            outError);
}

bool cookRuntimeAuxiliaries(
    nlohmann::json& outManifest,
    std::string& outError) {
    std::vector<std::string> models;
    return runtimeAuxiliaryModels(models, outError) &&
        cookModelSet(
            "Runtime auxiliary",
            "Object",
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
    std::string id;
    std::string fileStem;
    std::string displayName;
    std::string canonicalRoot;
    std::string sourceBoundary;
    std::string motionDriver;
    std::vector<std::uint32_t> meshIndices;
    nlohmann::json semanticGroups;
    bool referencesRouteScene = false;
    std::int32_t derivedTreeMeshIndex = -1;
    std::uint32_t expectedTreeInstanceCount = 0u;
    std::int32_t derivedTerrainMeshIndex = -1;
    std::int32_t derivedTerrainAssemblyIndex = -1;
};

std::string route1CanonicalMeshDisplayName(
    std::uint32_t index) {
    if (index == 0u) return "Ground Blend Overlay";
    if (index >= 1u && index <= 9u) {
        return "Road Stone Patch " +
            std::to_string(index);
    }
    if (index == 16u) return "Small Ground Vegetation Layer";
    if (index == 17u) return "Source Flower Layer";
    if (index >= 18u && index <= 25u) {
        return "Ground Foliage Layer " +
            std::to_string(index - 17u);
    }
    if (index == 26u) return "Rock and Foliage Layer";
    if (index == 27u) return "Cliff Foliage Layer 1";
    if (index == 28u) return "Cliff Foliage Layer 2";
    if (index == 36u) return "Route Ground Plane";
    if (index == 37u) return "Route Sign";
    return "Route 1 Source Mesh " + std::to_string(index);
}

std::string zeroPadded(
    std::uint32_t value,
    std::size_t width) {
    std::string out = std::to_string(value);
    while (out.size() < width) {
        out.insert(out.begin(), '0');
    }
    return out;
}

std::vector<Route1PrefabDefinition> route1PrefabDefinitions() {
    using Definition = Route1PrefabDefinition;
    std::vector<Definition> definitions{
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
            "route1/tree_001",
            "tree_001",
            "Tree 001",
            game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            "derived_tree_archetype_from_exact_route_mesh",
            "none_source_vertex_programs_are_static",
            {10u},
            {{"tree", nlohmann::json::array({10u})}},
            true,
            10,
            11u},
        Definition{
            "route1/tree_002",
            "tree_002",
            "Tree 002",
            game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            "derived_tree_archetype_from_exact_route_mesh",
            "none_source_vertex_programs_are_static",
            {11u},
            {{"tree", nlohmann::json::array({11u})}},
            true,
            11,
            11u},
        Definition{
            "route1/tree_003",
            "tree_003",
            "Tree 003",
            game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            "derived_tree_archetype_from_exact_route_mesh",
            "none_source_vertex_programs_are_static",
            {12u},
            {{"tree", nlohmann::json::array({12u})}},
            true,
            12,
            12u},
        Definition{
            "route1/tree_004",
            "tree_004",
            "Tree 004",
            game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            "derived_tree_archetype_from_exact_route_mesh",
            "none_source_vertex_programs_are_static",
            {13u},
            {{"tree", nlohmann::json::array({13u})}},
            true,
            13,
            2u},
        Definition{
            "route1/tree_005",
            "tree_005",
            "Tree 005",
            game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            "derived_tree_archetype_from_exact_route_mesh",
            "none_source_vertex_programs_are_static",
            {14u},
            {{"tree", nlohmann::json::array({14u})}},
            true,
            14,
            2u},
        Definition{
            "route1/tree_006",
            "tree_006",
            "Tree 006",
            game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            "derived_tree_archetype_from_exact_route_mesh",
            "none_source_vertex_programs_are_static",
            {15u},
            {{"tree", nlohmann::json::array({15u})}},
            true,
            15,
            9u},
        Definition{
            .id = "route1/autochess_board_ground_patch",
            .fileStem = "autochess_board_ground_patch",
            .displayName = "Autochess Board Ground Patch",
            .canonicalRoot =
                game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            .sourceBoundary =
                "generated_quad_from_exact_gamefreak_ground_attributes",
            .motionDriver =
                "none_source_vertex_programs_are_static",
            .meshIndices = {25u},
            .semanticGroups = {
                {"gameplay_board_ground_patch",
                 nlohmann::json::array({25u})}},
            .referencesRouteScene = true},
        Definition{
            .id = "route1/terrain_tileset",
            .fileStem = "terrain_tileset",
            .displayName = "Route 1 Terrain Tile Set",
            .canonicalRoot =
                game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            .sourceBoundary =
                "generated_100cm_cells_from_exact_route_lawn_and_cliff_attributes",
            .motionDriver =
                "tile_top_ramp_and_ledge_seams_derived_by_runtime",
            .meshIndices = {25u, 31u, 32u},
            .semanticGroups = {
                {"light_lawn_source", nlohmann::json::array({25u})},
                {"dark_lawn_source", nlohmann::json::array({31u})},
                {"cliff_edge_source", nlohmann::json::array({32u})},
                {"dirt_path_connection_masks",
                 nlohmann::json::array({
                     "path_0", "path_1", "path_2", "path_3",
                     "path_4", "path_5", "path_6", "path_7",
                     "path_8", "path_9", "path_10", "path_11",
                     "path_12", "path_13", "path_14", "path_15"})},
                {"dirt_path_connection_bits",
                 {
                     {"north", 1u},
                     {"east", 2u},
                     {"south", 4u},
                     {"west", 8u},
                     {"interpretation",
                      "set_bit_connects_dirt_unset_bit_uses_source_grass_edge"}}}},
            .referencesRouteScene = true},
    };
    for (const std::uint32_t meshIndex : {
             0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u,
             16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u,
             25u, 26u, 27u, 28u, 36u, 37u}) {
        const std::string stem =
            "source_mesh_" + zeroPadded(meshIndex, 3u);
        definitions.push_back(
            Definition{
                .id = "route1/" + stem,
                .fileStem = stem,
                .displayName =
                    route1CanonicalMeshDisplayName(meshIndex),
                .canonicalRoot =
                    game::runtime::lgpe_route1_runtime::
                        kCanonicalRoot,
                .sourceBoundary =
                    "exact_source_mesh_group",
                .motionDriver =
                    "none_source_vertex_programs_are_static",
                .meshIndices = {meshIndex},
                .semanticGroups = {
                    {"source_mesh_group",
                     nlohmann::json::array({meshIndex})}},
                .referencesRouteScene = true});
    }
    namespace terrain =
        game::runtime::lgpe_route1_terrain_assemblies;
    for (std::uint32_t meshIndex = 29u;
         meshIndex <= 35u;
         ++meshIndex) {
        const std::uint32_t count =
            terrain::expectedAssemblyCount(meshIndex);
        for (std::uint32_t assemblyIndex = 0u;
             assemblyIndex < count;
             ++assemblyIndex) {
            const std::string stem =
                "terrain_mesh_" +
                zeroPadded(meshIndex, 3u) +
                "_assembly_" +
                zeroPadded(assemblyIndex + 1u, 2u);
            definitions.push_back(
                Definition{
                    .id = "route1/" + stem,
                    .fileStem = stem,
                    .displayName =
                        "Route 1 Terrain Assembly " +
                        std::to_string(meshIndex) + "." +
                        std::to_string(assemblyIndex + 1u),
                    .canonicalRoot =
                        game::runtime::lgpe_route1_runtime::
                            kCanonicalRoot,
                    .sourceBoundary =
                        "connected_gamefreak_terrain_body_cap_pair",
                    .motionDriver =
                        "none_source_vertex_programs_are_static",
                    .meshIndices = {meshIndex},
                    .semanticGroups = {
                        {"terrain_assembly",
                         nlohmann::json::array({meshIndex})}},
                    .referencesRouteScene = true,
                    .derivedTerrainMeshIndex =
                        static_cast<std::int32_t>(meshIndex),
                    .derivedTerrainAssemblyIndex =
                        static_cast<std::int32_t>(
                            assemblyIndex)});
        }
    }
    return definitions;
}

bool deriveRouteTreeSelector(
    const engine::assets::lgpe::CanonicalScene& source,
    const Route1PrefabDefinition& definition,
    nlohmann::json& outDerivation,
    std::string& outError) {
    const auto meshIt = std::find_if(
        source.meshes.begin(),
        source.meshes.end(),
        [&](const auto& mesh) {
            return mesh.sourceIndex ==
                static_cast<std::uint32_t>(
                    definition.derivedTreeMeshIndex);
        });
    if (meshIt == source.meshes.end()) {
        outError =
            "Derived Route 1 tree mesh is missing: " +
            std::to_string(definition.derivedTreeMeshIndex);
        return false;
    }
    const auto groupIt = std::find_if(
        meshIt->polygonGroups.begin(),
        meshIt->polygonGroups.end(),
        [](const auto& group) {
            return group.materialIndex == 2u;
        });
    if (groupIt == meshIt->polygonGroups.end() ||
        groupIt->indices.empty()) {
        outError =
            "Derived Route 1 tree has no trunk material topology.";
        return false;
    }

    std::vector<std::int32_t> parents(
        meshIt->vertices.size(),
        -1);
    const auto findRoot = [&](std::uint32_t value) {
        std::uint32_t root = value;
        while (parents[root] !=
               static_cast<std::int32_t>(root)) {
            root = static_cast<std::uint32_t>(
                parents[root]);
        }
        std::uint32_t current = value;
        while (current != root) {
            const std::uint32_t next =
                static_cast<std::uint32_t>(
                    parents[current]);
            parents[current] =
                static_cast<std::int32_t>(root);
            current = next;
        }
        return root;
    };
    const auto join = [&](std::uint32_t left, std::uint32_t right) {
        const std::uint32_t leftRoot = findRoot(left);
        const std::uint32_t rightRoot = findRoot(right);
        if (leftRoot != rightRoot) {
            parents[rightRoot] =
                static_cast<std::int32_t>(leftRoot);
        }
    };
    for (const std::uint32_t index : groupIt->indices) {
        if (index >= parents.size()) {
            outError =
                "Derived Route 1 tree trunk index is out of range.";
            return false;
        }
        parents[index] = static_cast<std::int32_t>(index);
    }
    for (std::size_t index = 0u;
         index + 2u < groupIt->indices.size();
         index += 3u) {
        join(groupIt->indices[index], groupIt->indices[index + 1u]);
        join(groupIt->indices[index], groupIt->indices[index + 2u]);
    }

    struct Component {
        std::uint32_t count = 0u;
        std::array<double, 3> sum{};
    };
    std::map<std::uint32_t, Component> components;
    for (std::uint32_t index = 0u;
         index < parents.size();
         ++index) {
        if (parents[index] < 0) continue;
        auto& component = components[findRoot(index)];
        ++component.count;
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            component.sum[axis] +=
                meshIt->vertices[index].position[axis];
        }
    }
    std::uint32_t largestComponent = 0u;
    for (const auto& [root, component] : components) {
        (void)root;
        largestComponent =
            std::max(largestComponent, component.count);
    }
    if (largestComponent == 0u) {
        outError =
            "Derived Route 1 tree trunk topology is empty.";
        return false;
    }

    std::vector<std::array<double, 3>> candidateCenters;
    for (const auto& [root, component] : components) {
        (void)root;
        if (component.count != largestComponent) continue;
        candidateCenters.push_back({
            component.sum[0] / component.count,
            component.sum[1] / component.count,
            component.sum[2] / component.count});
    }
    std::sort(
        candidateCenters.begin(),
        candidateCenters.end(),
        [](const auto& left, const auto& right) {
            return left[0] != right[0]
                ? left[0] < right[0]
                : left[2] < right[2];
        });

    struct Cluster {
        std::array<double, 3> sum{};
        std::uint32_t count = 0u;
    };
    constexpr double kClusterRadiusCm = 100.0;
    std::vector<Cluster> clusters;
    for (const auto& center : candidateCenters) {
        auto clusterIt = std::find_if(
            clusters.begin(),
            clusters.end(),
            [&](const Cluster& cluster) {
                const double x =
                    cluster.sum[0] / cluster.count;
                const double z =
                    cluster.sum[2] / cluster.count;
                const double dx = center[0] - x;
                const double dz = center[2] - z;
                return dx * dx + dz * dz <
                    kClusterRadiusCm * kClusterRadiusCm;
            });
        if (clusterIt == clusters.end()) {
            clusters.push_back(
                Cluster{center, 1u});
        } else {
            for (std::size_t axis = 0u; axis < 3u; ++axis) {
                clusterIt->sum[axis] += center[axis];
            }
            ++clusterIt->count;
        }
    }

    std::vector<std::array<double, 3>> centers;
    centers.reserve(clusters.size());
    for (const auto& cluster : clusters) {
        centers.push_back({
            cluster.sum[0] / cluster.count,
            cluster.sum[1] / cluster.count,
            cluster.sum[2] / cluster.count});
    }
    std::sort(
        centers.begin(),
        centers.end(),
        [](const auto& left, const auto& right) {
            return left[0] != right[0]
                ? left[0] < right[0]
                : left[2] < right[2];
        });
    if (centers.size() !=
        definition.expectedTreeInstanceCount) {
        outError =
            "Derived Route 1 tree instance count changed for " +
            std::string(definition.displayName) + ": expected " +
            std::to_string(
                definition.expectedTreeInstanceCount) +
            ", found " + std::to_string(centers.size());
        return false;
    }

    struct VertexRange {
        std::uint32_t first = 0u;
        std::uint32_t count = 0u;
        std::array<double, 3> center{};
    };
    struct GroupRanges {
        std::uint32_t polygonGroupIndex = 0u;
        std::uint32_t materialIndex = 0u;
        std::vector<VertexRange> instances;
    };
    std::vector<GroupRanges> groupRanges;
    groupRanges.reserve(meshIt->polygonGroups.size());
    for (std::size_t groupIndex = 0u;
         groupIndex < meshIt->polygonGroups.size();
         ++groupIndex) {
        const auto& group = meshIt->polygonGroups[groupIndex];
        if (group.indices.empty()) {
            outError =
                "Derived Route 1 tree polygon group is empty.";
            return false;
        }
        const auto [minimumIt, maximumIt] =
            std::minmax_element(
                group.indices.begin(),
                group.indices.end());
        const std::uint32_t minimum = *minimumIt;
        const std::uint32_t maximum = *maximumIt;
        if (maximum >= meshIt->vertices.size()) {
            outError =
                "Derived Route 1 tree polygon group index is out of range.";
            return false;
        }
        const std::uint32_t span = maximum - minimum + 1u;
        std::vector<bool> referenced(span, false);
        for (const std::uint32_t index : group.indices) {
            referenced[index - minimum] = true;
        }
        if (!std::all_of(
                referenced.begin(),
                referenced.end(),
                [](bool value) { return value; })) {
            outError =
                "Derived Route 1 tree source vertex range is not contiguous.";
            return false;
        }

        std::vector<std::uint32_t> boundaries{minimum};
        const std::uint32_t instanceCount =
            definition.expectedTreeInstanceCount;
        if (span % instanceCount == 0u) {
            const std::uint32_t block = span / instanceCount;
            for (std::uint32_t instance = 1u;
                 instance < instanceCount;
                 ++instance) {
                boundaries.push_back(minimum + instance * block);
            }
        } else {
            struct Gap {
                double distanceSquared = 0.0;
                std::uint32_t boundary = 0u;
            };
            std::vector<Gap> gaps;
            gaps.reserve(span - 1u);
            for (std::uint32_t index = minimum;
                 index < maximum;
                 ++index) {
                const auto& left =
                    meshIt->vertices[index].position;
                const auto& right =
                    meshIt->vertices[index + 1u].position;
                const double dx =
                    static_cast<double>(left[0]) - right[0];
                const double dz =
                    static_cast<double>(left[2]) - right[2];
                gaps.push_back(
                    Gap{dx * dx + dz * dz, index + 1u});
            }
            std::sort(
                gaps.begin(),
                gaps.end(),
                [](const Gap& left, const Gap& right) {
                    return left.distanceSquared !=
                            right.distanceSquared
                        ? left.distanceSquared >
                              right.distanceSquared
                        : left.boundary < right.boundary;
                });
            if (gaps.size() < instanceCount - 1u) {
                outError =
                    "Derived Route 1 tree source range has too few instance boundaries.";
                return false;
            }
            for (std::uint32_t index = 0u;
                 index + 1u < instanceCount;
                 ++index) {
                boundaries.push_back(gaps[index].boundary);
            }
            std::sort(boundaries.begin(), boundaries.end());
        }
        boundaries.push_back(maximum + 1u);
        if (boundaries.size() != instanceCount + 1u) {
            outError =
                "Derived Route 1 tree source instance boundary count changed.";
            return false;
        }

        GroupRanges ranges;
        ranges.polygonGroupIndex =
            static_cast<std::uint32_t>(groupIndex);
        ranges.materialIndex = group.materialIndex;
        ranges.instances.reserve(instanceCount);
        for (std::uint32_t instance = 0u;
             instance < instanceCount;
             ++instance) {
            const std::uint32_t first = boundaries[instance];
            const std::uint32_t count =
                boundaries[instance + 1u] - first;
            if (count == 0u) {
                outError =
                    "Derived Route 1 tree source instance range is empty.";
                return false;
            }
            VertexRange range{first, count, {}};
            for (std::uint32_t index = first;
                 index < first + count;
                 ++index) {
                for (std::size_t axis = 0u;
                     axis < 3u;
                     ++axis) {
                    range.center[axis] +=
                        meshIt->vertices[index].position[axis];
                }
            }
            for (double& value : range.center) {
                value /= count;
            }
            ranges.instances.push_back(range);
        }
        groupRanges.push_back(std::move(ranges));
    }

    const auto trunkRangesIt = std::find_if(
        groupRanges.begin(),
        groupRanges.end(),
        [](const GroupRanges& group) {
            return group.materialIndex == 2u;
        });
    if (trunkRangesIt == groupRanges.end()) {
        outError =
            "Derived Route 1 tree source instance blocks have no trunk group.";
        return false;
    }
    for (const auto& group : groupRanges) {
        for (std::size_t instance = 0u;
             instance < group.instances.size();
             ++instance) {
            std::size_t nearest = 0u;
            double nearestDistance =
                std::numeric_limits<double>::max();
            for (std::size_t candidate = 0u;
                 candidate < trunkRangesIt->instances.size();
                 ++candidate) {
                const double dx =
                    group.instances[instance].center[0] -
                    trunkRangesIt->instances[candidate].center[0];
                const double dz =
                    group.instances[instance].center[2] -
                    trunkRangesIt->instances[candidate].center[2];
                const double distance = dx * dx + dz * dz;
                if (distance < nearestDistance) {
                    nearest = candidate;
                    nearestDistance = distance;
                }
            }
            if (nearest != instance) {
                outError =
                    "Derived Route 1 tree material instance ordering is inconsistent.";
                return false;
            }
        }
    }

    std::uint32_t selectedVertexCount = 0u;
    for (const auto& group : groupRanges) {
        selectedVertexCount +=
            group.instances.front().count;
    }

    game::runtime::lgpe_route1_tree_instances::
        MeshPartition runtimePartition;
    if (!game::runtime::lgpe_route1_tree_instances::
            derivePartition(
                *meshIt,
                definition.expectedTreeInstanceCount,
                runtimePartition,
                &outError) ||
        runtimePartition.polygonGroups.size() !=
            groupRanges.size()) {
        if (outError.empty()) {
            outError =
                "Route 1 tree runtime/editor partition disagrees "
                "with Forge.";
        }
        return false;
    }
    for (std::size_t groupIndex = 0u;
         groupIndex < groupRanges.size();
         ++groupIndex) {
        const auto& forgeGroup =
            groupRanges[groupIndex];
        const auto& runtimeGroup =
            runtimePartition.polygonGroups[groupIndex];
        if (forgeGroup.polygonGroupIndex !=
                runtimeGroup.polygonGroupIndex ||
            forgeGroup.materialIndex !=
                runtimeGroup.materialIndex ||
            forgeGroup.instances.size() !=
                runtimeGroup.instances.size()) {
            outError =
                "Route 1 tree runtime/editor group partition "
                "disagrees with Forge.";
            return false;
        }
        for (std::size_t instance = 0u;
             instance < forgeGroup.instances.size();
             ++instance) {
            if (forgeGroup.instances[instance].first !=
                    runtimeGroup.instances[instance].first ||
                forgeGroup.instances[instance].count !=
                    runtimeGroup.instances[instance].count) {
                outError =
                    "Route 1 tree runtime/editor instance boundary "
                    "disagrees with Forge.";
                return false;
            }
        }
    }

    outDerivation = {
        {"kind", "source_repeated_instance_vertex_blocks"},
        {"mesh_index", definition.derivedTreeMeshIndex},
        {"trunk_material_index", 2},
        {"largest_trunk_component_vertices", largestComponent},
        {"cluster_radius_cm", kClusterRadiusCm},
        {"connected_trunk_instance_count", centers.size()},
        {"triangle_partition",
         "source_polygon_group_vertex_ranges"},
        {"selected_source_instance", 0},
        {"selected_vertex_count", selectedVertexCount},
        {"instance_centers_cm", nlohmann::json::array()},
        {"polygon_groups", nlohmann::json::array()},
    };
    for (const auto& range : trunkRangesIt->instances) {
        outDerivation["instance_centers_cm"].push_back(
            {range.center[0], range.center[1], range.center[2]});
    }
    for (const auto& group : groupRanges) {
        nlohmann::json record{
            {"polygon_group_index", group.polygonGroupIndex},
            {"material_index", group.materialIndex},
            {"instances", nlohmann::json::array()},
        };
        for (const auto& range : group.instances) {
            record["instances"].push_back({
                {"first_vertex", range.first},
                {"vertex_count", range.count},
            });
        }
        outDerivation["polygon_groups"].push_back(
            std::move(record));
    }
    return true;
}

bool deriveRouteTerrainSelector(
    const engine::assets::lgpe::CanonicalScene& source,
    const Route1PrefabDefinition& definition,
    nlohmann::json& outDerivation,
    std::string& outError) {
    namespace terrain =
        game::runtime::lgpe_route1_terrain_assemblies;
    const auto meshIt = std::find_if(
        source.meshes.begin(),
        source.meshes.end(),
        [&](const auto& mesh) {
            return mesh.sourceIndex ==
                static_cast<std::uint32_t>(
                    definition.derivedTerrainMeshIndex);
        });
    if (meshIt == source.meshes.end()) {
        outError =
            "Derived Route 1 terrain mesh is missing: " +
            std::to_string(
                definition.derivedTerrainMeshIndex);
        return false;
    }
    terrain::MeshPartition partition;
    if (!terrain::derivePartition(
            *meshIt,
            partition,
            &outError)) {
        return false;
    }
    const std::size_t assemblyIndex =
        static_cast<std::size_t>(
            definition.derivedTerrainAssemblyIndex);
    if (assemblyIndex >= partition.assemblies.size()) {
        outError =
            "Derived Route 1 terrain assembly is out of range.";
        return false;
    }
    const auto& assembly =
        partition.assemblies[assemblyIndex];
    outDerivation = {
        {"kind", "source_connected_terrain_body_cap_pair"},
        {"mesh_index", definition.derivedTerrainMeshIndex},
        {"assembly_index",
         definition.derivedTerrainAssemblyIndex},
        {"expected_assembly_count",
         partition.assemblies.size()},
        {"profile_role", assembly.profileRole},
        {"source_pivot_cm", assembly.sourcePivotCm},
        {"bounds_minimum_cm", assembly.boundsMinimum},
        {"bounds_maximum_cm", assembly.boundsMaximum},
        {"pairing_evidence",
         "millimetre-quantized seam connectivity plus body/cap bounds matching"},
    };
    return true;
}

bool cookRoute1Prefabs(
    nlohmann::json& outManifest,
    std::string& outError) {
    outManifest = nlohmann::json::array();
    game::assets::DevAssetStore root(".");
    engine::assets::lgpe::CanonicalScene routeSource;
    if (!engine::assets::lgpe::loadCanonicalScene(
            root,
            game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            routeSource,
            &outError)) {
        return false;
    }
    std::vector<std::uint8_t> routeArchiveBytes;
    if (!readFile(
            kRoute1Archive,
            routeArchiveBytes,
            outError)) {
        return false;
    }
    engine::assets::phlosion::SceneArchiveStore routeArchive;
    if (!routeArchive.load(
            root,
            kRoute1Archive,
            &outError)) {
        return false;
    }
    for (const auto& definition : route1PrefabDefinitions()) {
        std::map<
            std::string,
            engine::assets::phlosion::SceneArchiveFile> files;
        if (!definition.referencesRouteScene) {
            if (!addVirtualDirectory(
                    definition.canonicalRoot,
                    files,
                    outError)) {
                return false;
            }
        }
        std::vector<
            engine::assets::phlosion::SceneArchiveFile> archiveFiles;
        archiveFiles.reserve(files.size());
        for (auto& [path, file] : files) {
            (void)path;
            archiveFiles.push_back(std::move(file));
        }

        nlohmann::json metadata{
            {"schema_version", 1},
            {"display_name", definition.displayName},
            {"canonical_root", definition.canonicalRoot},
            {"canonical_storage",
             definition.referencesRouteScene
                 ? "route_scene_dependency"
                 : "embedded"},
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
        std::vector<engine::assets::phrc::Dependency>
            dependencies;
        if (definition.referencesRouteScene) {
            nlohmann::json derivation;
            if (definition.derivedTreeMeshIndex >= 0) {
                if (!deriveRouteTreeSelector(
                        routeSource,
                        definition,
                        derivation,
                        outError)) {
                    return false;
                }
                metadata["selector"]["derivation"] =
                    std::move(derivation);
            } else if (
                definition.derivedTerrainMeshIndex >= 0) {
                if (!deriveRouteTerrainSelector(
                        routeSource,
                        definition,
                        derivation,
                        outError)) {
                    return false;
                }
                metadata["selector"]["derivation"] =
                    std::move(derivation);
            }
            metadata["scene_archive"] = kRoute1Archive;
            dependencies.push_back(
                engine::assets::phrc::Dependency{
                    kRoute1Archive,
                    engine::assets::phrc::contentHash64(
                        routeArchiveBytes),
                    engine::assets::phrc::
                        kDependencyRequired});
        }

        std::vector<std::uint8_t> bytes;
        if (!engine::assets::phlosion::encodePrefabArchive(
                definition.id,
                "LgpeEnvironment",
                metadata.dump(),
                std::move(archiveFiles),
                std::move(dependencies),
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
        const bool sourceLoaded =
            definition.referencesRouteScene
            ? engine::assets::lgpe::loadCanonicalScene(
                  routeArchive,
                  definition.canonicalRoot,
                  source,
                  &outError)
            : engine::assets::lgpe::loadCanonicalScene(
                  prefab,
                  definition.canonicalRoot,
                  source,
                  &outError);
        if (!sourceLoaded) {
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

bool validateRoute1LayoutPrefabCoverage(
    game::runtime::lgpe_route1_runtime::RuntimeEnvironment&
        environment,
    std::string& outError) {
    const auto definitions = route1PrefabDefinitions();
    std::set<std::string> prefabIds;
    for (const auto& definition : definitions) {
        prefabIds.insert(definition.id);
    }
    std::set<std::string> stableIds;
    std::size_t importedCount = 0u;
    std::size_t terrainCount = 0u;
    std::size_t boardGroundPrototypeCount = 0u;
    for (const auto& object : environment.layoutObjects()) {
        if (!stableIds.insert(object.stableId).second ||
            object.prefabAssetId.empty() ||
            !prefabIds.contains(object.prefabAssetId)) {
            outError =
                "Route 1 editable object does not have one valid source-bound PHLO prefab: " +
                object.stableId;
            return false;
        }
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            if (!std::isfinite(object.boundsMinimumCm[axis]) ||
                !std::isfinite(object.boundsMaximumCm[axis]) ||
                object.boundsMinimumCm[axis] >
                    object.boundsMaximumCm[axis]) {
                outError =
                    "Route 1 editable object has invalid source bounds: " +
                    object.stableId;
                return false;
            }
        }
        if (!object.authored) {
            ++importedCount;
        }
        if (object.targetKind ==
            "canonical_terrain_assembly") {
            ++terrainCount;
        }
        if (object.targetKind ==
            "gameplay_board_ground_prototype") {
            ++boardGroundPrototypeCount;
            if (!object.suppressed ||
                object.prefabAssetId !=
                    "route1/autochess_board_ground_patch") {
                outError =
                    "The Route 1 gameplay-board ground prototype must remain hidden until explicitly instanced.";
                return false;
            }
        }
    }
    if (importedCount != 156u || terrainCount != 23u ||
        boardGroundPrototypeCount != 1u) {
        outError =
            "Route 1 editable source inventory changed: expected 156 imported objects, 23 terrain assemblies, and one hidden board-ground prototype, found " +
            std::to_string(importedCount) + " and " +
            std::to_string(terrainCount) + " and " +
            std::to_string(boardGroundPrototypeCount) + ".";
        return false;
    }

    const auto sourceTile = std::find_if(
        environment.terrainTiles().begin(),
        environment.terrainTiles().end(),
        [](const auto& tile) {
            return tile.sourceOccupied && !tile.authored;
        });
    if (sourceTile == environment.terrainTiles().end()) {
        outError =
            "Route 1 did not expose an occupied source terrain cell for editor validation.";
        return false;
    }
    const std::int32_t sourceGridX = sourceTile->gridX;
    const std::int32_t sourceGridZ = sourceTile->gridZ;
    const std::int32_t editedElevationLevel =
        sourceTile->elevationLevel + 1;
    const std::string sourceSurface = sourceTile->surface;
    const auto previousLayout = environment.layout();
    auto editedLayout = previousLayout;
    const std::string tileStableId =
        game::runtime::lgpe_route1_runtime::
            route1TerrainTileStableId(
                sourceGridX,
                sourceGridZ);
    editedLayout.authoredTerrainTiles.push_back(
        game::runtime::lgpe_route1_runtime::AuthoredTerrainTile{
            .stableId = tileStableId,
            .displayName = "Forge Terrain Edit Proof",
            .categoryPath = "Environment/Terrain/Tiles",
            .tileSetAssetId = "route1/terrain_tileset",
            .gridX = sourceGridX,
            .gridZ = sourceGridZ,
            .elevationLevel = editedElevationLevel,
            .surface = sourceSurface,
            .shape = "flat",
            .reason = "forge_validation"});
    if (!environment.applyBoardLayout(editedLayout, &outError)) {
        outError =
            "Route 1 rejected a valid reversible terrain-cell edit: " +
            outError;
        return false;
    }
    const auto editedTile = std::find_if(
        environment.terrainTiles().begin(),
        environment.terrainTiles().end(),
        [&](const auto& tile) {
            return tile.gridX == sourceGridX &&
                tile.gridZ == sourceGridZ;
        });
    const bool editedTileValid =
        editedTile != environment.terrainTiles().end() &&
        editedTile->authored &&
        editedTile->elevationLevel == editedElevationLevel;
    const bool restored =
        environment.applyBoardLayout(previousLayout, &outError);
    if (!editedTileValid || !restored) {
        outError =
            "Route 1 terrain-cell edit/restore validation failed: " +
            outError;
        return false;
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
    game::runtime::lgpe_route1_runtime::BoardLayoutTransform
        projectRegistration;
    engine::assets::phlosion::AuthoredSceneDocument authoredScene;
    if (!game::runtime::lgpe_route1_runtime::
            loadBoardLayoutTransform(
                root,
                game::runtime::lgpe_route1_runtime::
                    kBoardLayoutManifestPath,
                projectRegistration,
                &outError) ||
        !environment.applyBoardLayout(
            projectRegistration,
            &outError) ||
        !engine::assets::phlosion::
            loadAuthoredSceneDocument(
                root,
                kRoute1AuthoredScene,
                authoredScene,
                &outError) ||
        !environment.applyAuthoredScene(
            authoredScene,
            &outError)) {
        outError =
            "Route 1 project composition validation failed: " +
            outError;
        return false;
    }
    if (!validateRoute1LayoutPrefabCoverage(
            environment,
            outError)) {
        return false;
    }
    std::vector<std::uint8_t> authoredSceneBytes;
    if (!readFile(
            kRoute1AuthoredScene,
            authoredSceneBytes,
            outError)) {
        return false;
    }
    const auto& stats = environment.stats();
    outManifest = {
        {"scene", kRoute1Archive},
        {"scene_fnv1a64",
            hex64(engine::assets::phrc::contentHash64(archiveBytes))},
        {"authored_scene", kRoute1AuthoredScene},
        {"authored_scene_fnv1a64",
            hex64(
                engine::assets::phrc::contentHash64(
                    authoredSceneBytes))},
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
            stats.placedVegetationInstanceCount},
        {"editable_source_objects", 156u},
        {"editable_terrain_assemblies", 23u}};
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
    game::runtime::lgpe_route1_runtime::BoardLayoutTransform
        projectRegistration;
    engine::assets::phlosion::AuthoredSceneDocument authoredScene;
    if (!game::runtime::lgpe_route1_runtime::
            loadBoardLayoutTransform(
                root,
                game::runtime::lgpe_route1_runtime::
                    kBoardLayoutManifestPath,
                projectRegistration,
                &outError) ||
        !environment.applyBoardLayout(
            projectRegistration,
            &outError) ||
        !engine::assets::phlosion::
            loadAuthoredSceneDocument(
                root,
                kRoute1AuthoredScene,
                authoredScene,
                &outError) ||
        !environment.applyAuthoredScene(
            authoredScene,
            &outError)) {
        return false;
    }
    if (!validateRoute1LayoutPrefabCoverage(
            environment,
            outError)) {
        return false;
    }
    std::string createdStableId;
    if (!environment.duplicateLayoutObject(
            "buildmodel-vegetation/grass02/record-21",
            createdStableId,
            &outError)) {
        outError =
            "Authored-scene edit/export validation failed: " +
            outError;
        return false;
    }
    const std::string editedDocumentText =
        engine::assets::phlosion::
            serializeAuthoredSceneDocument(
                environment.authoredScene());
    engine::assets::phlosion::AuthoredSceneDocument
        editedRoundTrip;
    if (!engine::assets::phlosion::
            parseAuthoredSceneDocument(
                editedDocumentText,
                editedRoundTrip,
                &outError) ||
        std::none_of(
            editedRoundTrip.nodes.begin(),
            editedRoundTrip.nodes.end(),
            [&](const auto& node) {
                return node.id == createdStableId &&
                    node.prefabInstance.has_value();
            }) ||
        !environment.applyAuthoredScene(
            editedRoundTrip,
            &outError) ||
        !environment.applyAuthoredScene(
            authoredScene,
            &outError)) {
        outError =
            "Authored-scene deterministic edit round trip failed: " +
            outError;
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
        const bool sourceLoaded =
            definition.referencesRouteScene
            ? engine::assets::lgpe::loadCanonicalScene(
                  scene,
                  definition.canonicalRoot,
                  source,
                  &outError)
            : engine::assets::lgpe::loadCanonicalScene(
                  prefab,
                  definition.canonicalRoot,
                  source,
                  &outError);
        if (!sourceLoaded) {
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

bool inspectRoute1SourceTerrainTile(
    std::int32_t gridX,
    std::int32_t gridZ,
    std::string& outError) {
    game::assets::DevAssetStore root(".");
    engine::assets::phlosion::SceneArchiveStore scene;
    if (!scene.load(root, kRoute1Archive, &outError)) {
        return false;
    }
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
    const auto found = std::find_if(
        environment.terrainTiles().begin(),
        environment.terrainTiles().end(),
        [&](const auto& tile) {
            return tile.gridX == gridX && tile.gridZ == gridZ;
        });
    if (found == environment.terrainTiles().end()) {
        outError =
            "Route 1 source terrain coordinate is outside the recovered grid.";
        return false;
    }
    std::cout
        << "[Phlosion Forge] Route 1 source terrain ("
        << gridX << ", " << gridZ << ")"
        << " occupied=" << (found->sourceOccupied ? 1 : 0)
        << " elevation_level=" << found->sourceElevationLevel
        << " surface=" << found->sourceSurface
        << " shape=" << found->sourceShape << "\n";
    return true;
}

bool inspectRoute1SourceTerrainJunction(
    std::int32_t gridX,
    std::int32_t gridZ,
    const fs::path& outputPath,
    std::string& outError) {
    game::assets::DevAssetStore root(".");
    engine::assets::phlosion::SceneArchiveStore scene;
    if (!scene.load(root, kRoute1Archive, &outError)) {
        return false;
    }
    engine::assets::lgpe::CanonicalScene source;
    if (!engine::assets::lgpe::loadCanonicalScene(
            scene,
            game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            source,
            &outError)) {
        return false;
    }

    constexpr float tileSizeCm = 100.0f;
    constexpr float seamEpsilonCm = 0.02f;
    const float west = static_cast<float>(gridX) * tileSizeCm;
    const float east = west + tileSizeCm;
    const float south = static_cast<float>(gridZ) * tileSizeCm;
    const float north = south + tileSizeCm;
    const auto transformPoint = [](
        const std::array<float, 16>& matrix,
        const std::array<float, 3>& point) {
        return std::array<float, 3>{
            matrix[0] * point[0] + matrix[4] * point[1] +
                matrix[8] * point[2] + matrix[12],
            matrix[1] * point[0] + matrix[5] * point[1] +
                matrix[9] * point[2] + matrix[13],
            matrix[2] * point[0] + matrix[6] * point[1] +
                matrix[10] * point[2] + matrix[14]};
    };
    const auto cellFor = [&](const std::array<float, 3>& point) {
        return std::array<std::int32_t, 2>{
            static_cast<std::int32_t>(
                std::floor(point[0] / tileSizeCm)),
            static_cast<std::int32_t>(
                std::floor(point[2] / tileSizeCm))};
    };
    const auto roleFor = [](
        std::uint32_t meshIndex,
        std::uint32_t materialIndex) -> std::string_view {
        if (meshIndex >= 29u && meshIndex <= 36u &&
            materialIndex == 19u) {
            return "source_ground";
        }
        if (meshIndex >= 29u && meshIndex <= 36u) {
            return "terrain_cliff_or_fringe";
        }
        if (meshIndex <= 9u) {
            return "ground_overlay";
        }
        if (meshIndex >= 16u && meshIndex <= 28u) {
            return "terrain_cleanup_or_foliage";
        }
        return "outside_runtime_terrain_mask";
    };

    nlohmann::json triangles = nlohmann::json::array();
    std::map<std::string, std::size_t> summary;
    for (const auto& mesh : source.meshes) {
        for (std::size_t groupIndex = 0u;
             groupIndex < mesh.polygonGroups.size();
             ++groupIndex) {
            const auto& group = mesh.polygonGroups[groupIndex];
            if (group.primitiveType != "triangles" &&
                group.primitiveType != "Triangles") {
                continue;
            }
            const bool runtimeTerrainGeometry =
                mesh.sourceIndex <= 9u ||
                (mesh.sourceIndex >= 16u &&
                 mesh.sourceIndex <= 36u);
            if (!runtimeTerrainGeometry) {
                continue;
            }
            const std::string_view role = roleFor(
                mesh.sourceIndex, group.materialIndex);
            for (std::size_t index = 0u;
                 index + 2u < group.indices.size();
                 index += 3u) {
                const std::array<std::uint32_t, 3> vertexIndices{
                    group.indices[index],
                    group.indices[index + 1u],
                    group.indices[index + 2u]};
                if (std::any_of(
                        vertexIndices.begin(),
                        vertexIndices.end(),
                        [&](std::uint32_t vertexIndex) {
                            return vertexIndex >= mesh.vertices.size();
                        })) {
                    continue;
                }
                std::array<std::array<float, 3>, 3> positions{};
                std::array<float, 3> minimum{
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max()};
                std::array<float, 3> maximum{
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest()};
                std::array<float, 3> centroid{};
                for (std::size_t corner = 0u;
                     corner < positions.size();
                     ++corner) {
                    positions[corner] = transformPoint(
                        mesh.transform,
                        mesh.vertices[vertexIndices[corner]].position);
                    for (std::size_t axis = 0u; axis < 3u; ++axis) {
                        minimum[axis] = std::min(
                            minimum[axis], positions[corner][axis]);
                        maximum[axis] = std::max(
                            maximum[axis], positions[corner][axis]);
                        centroid[axis] += positions[corner][axis] / 3.0f;
                    }
                }
                const bool crossesEastSeam =
                    minimum[0] <= east + seamEpsilonCm &&
                    maximum[0] >= east - seamEpsilonCm &&
                    minimum[2] <= north + seamEpsilonCm &&
                    maximum[2] >= south - seamEpsilonCm;
                const bool crossesNorthSeam =
                    minimum[2] <= north + seamEpsilonCm &&
                    maximum[2] >= north - seamEpsilonCm &&
                    minimum[0] <= east + seamEpsilonCm &&
                    maximum[0] >= west - seamEpsilonCm;
                if (!crossesEastSeam && !crossesNorthSeam) {
                    continue;
                }
                nlohmann::json vertexRecords = nlohmann::json::array();
                for (std::size_t corner = 0u;
                     corner < positions.size();
                     ++corner) {
                    const auto& vertex =
                        mesh.vertices[vertexIndices[corner]];
                    vertexRecords.push_back({
                        {"source_vertex_index", vertexIndices[corner]},
                        {"position_cm", positions[corner]},
                        {"cell", cellFor(positions[corner])},
                        {"uv0", vertex.texcoords[0]},
                        {"uv1", vertex.texcoords[1]},
                        {"uv2", vertex.texcoords[2]},
                        {"color0", vertex.colors[0]}});
                }
                std::string materialName;
                std::string shaderGroup;
                if (group.materialIndex < source.materials.size()) {
                    materialName = source.materials[group.materialIndex].name;
                    shaderGroup =
                        source.materials[group.materialIndex].shaderGroup;
                }
                triangles.push_back({
                    {"mesh_index", mesh.sourceIndex},
                    {"mesh_name", mesh.name},
                    {"polygon_group", groupIndex},
                    {"triangle_in_group", index / 3u},
                    {"material_index", group.materialIndex},
                    {"material_name", materialName},
                    {"shader_group", shaderGroup},
                    {"role", role},
                    {"crosses_east_seam", crossesEastSeam},
                    {"crosses_north_seam", crossesNorthSeam},
                    {"centroid_cm", centroid},
                    {"centroid_cell", cellFor(centroid)},
                    {"bounds_minimum_cm", minimum},
                    {"bounds_maximum_cm", maximum},
                    {"vertices", std::move(vertexRecords)}});
                const std::string summaryKey =
                    std::string(role) + "|material-" +
                    std::to_string(group.materialIndex) + "|" +
                    materialName;
                ++summary[summaryKey];
            }
        }
    }

    nlohmann::json summaryJson = nlohmann::json::object();
    for (const auto& [key, count] : summary) {
        summaryJson[key] = count;
    }
    const nlohmann::json report{
        {"schema_version", 1},
        {"kind", "lgpe_route1_source_terrain_junction_report"},
        {"source_profile_id", source.profileId},
        {"junction", {
            {"southwest_cell", {gridX, gridZ}},
            {"southeast_cell", {gridX + 1, gridZ}},
            {"northwest_cell", {gridX, gridZ + 1}},
            {"northeast_cell", {gridX + 1, gridZ + 1}},
            {"east_seam_x_cm", east},
            {"north_seam_z_cm", north}}},
        {"triangle_count", triangles.size()},
        {"summary", std::move(summaryJson)},
        {"triangles", std::move(triangles)}};
    if (!writeJson(outputPath, report, outError)) {
        return false;
    }
    std::cout
        << "[Phlosion Forge] Wrote Route 1 source junction report for ("
        << gridX << ", " << gridZ << ") to "
        << outputPath.string() << ".\n";
    return true;
}

void usage() {
    std::cerr
        << "Usage: PhlosionForge "
        << "<cook-all|cook-pokemon|cook-runtime|cook-route1|validate>\n"
        << "       PhlosionForge inspect-route1-source-tile <x> <z>\n"
        << "       PhlosionForge inspect-route1-source-junction <x> <z> <output.json>\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 5 &&
        std::string_view(argv[1]) ==
            "inspect-route1-source-junction") {
        try {
            const auto gridX = static_cast<std::int32_t>(
                std::stoi(argv[2]));
            const auto gridZ = static_cast<std::int32_t>(
                std::stoi(argv[3]));
            std::string error;
            if (!inspectRoute1SourceTerrainJunction(
                    gridX, gridZ, argv[4], error)) {
                std::cerr
                    << "[Phlosion Forge] ERROR: "
                    << error << "\n";
                return 1;
            }
            return 0;
        } catch (const std::exception&) {
            std::cerr
                << "[Phlosion Forge] ERROR: terrain coordinates must be integers.\n";
            return 2;
        }
    }
    if (argc == 4 &&
        std::string_view(argv[1]) ==
            "inspect-route1-source-tile") {
        try {
            const auto gridX = static_cast<std::int32_t>(
                std::stoi(argv[2]));
            const auto gridZ = static_cast<std::int32_t>(
                std::stoi(argv[3]));
            std::string error;
            if (!inspectRoute1SourceTerrainTile(
                    gridX, gridZ, error)) {
                std::cerr
                    << "[Phlosion Forge] ERROR: "
                    << error << "\n";
                return 1;
            }
            return 0;
        } catch (const std::exception&) {
            std::cerr
                << "[Phlosion Forge] ERROR: terrain coordinates must be integers.\n";
            return 2;
        }
    }
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
