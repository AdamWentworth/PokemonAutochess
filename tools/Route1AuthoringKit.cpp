#include "Route1AuthoringKit.h"
#include "engine/assets/phlosion/PhlosionAuthoredScene.h"
#include "engine/assets/phlosion/PhlosionSceneArchive.h"
#include "game/assets/DevAssetStore.h"
#include "game/assets/environment/PublishedEnvironmentScene.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"
#include "game/runtime/shared/scene/Route1TreeInstances.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace tools::route1_authoring {
namespace env = game::runtime::route1_environment;
namespace ph = engine::assets::phlosion;
using Json = nlohmann::json;

bool exportKit(const std::filesystem::path& output, std::string& error) {
    game::assets::DevAssetStore store(".");
    env::RuntimeEnvironment environment;
    if (!env::loadCookedEnvironment(store, environment, nullptr, &error)) return false;
    const auto identity = environment.sourceIdentity();
    Json result{
        {"kind", "phlosion_environment_authoring_kit"}, {"schema_version", 1},
        {"source", {{"profile_id", identity.profileId},
                    {"model_sha256", identity.modelSha256},
                    {"geometry_sha256", identity.geometrySha256},
                    {"coordinate_system", identity.coordinateSystem}}},
        {"objects", Json::array()}, {"trees", Json::array()}};
    for (const auto& object : environment.layoutObjects()) {
        if (object.authored) continue;
        const Json transform{{"translation", object.sourceTranslationCm},
            {"rotation_degrees", object.sourceRotationDegrees}, {"scale", object.sourceScale}};
        result["objects"].push_back({{"id", object.stableId},
            {"display_name", object.displayName}, {"prefab_asset_id", object.prefabAssetId},
            {"category_path", object.categoryPath},
            {"bounds_minimum_cm", object.boundsMinimumCm},
            {"bounds_maximum_cm", object.boundsMaximumCm},
            {"transform", transform},
            {"imported_source_binding", {{"target_kind", object.targetKind},
                {"logical_name", object.logicalName}, {"record_index", object.recordIndex},
                {"expected_source_transform", transform}}}});
    }
    const auto tileSnapshot = [&]() {
        Json tiles = Json::array();
        for (const auto& tile : environment.terrainTiles()) {
            tiles.push_back({{"x", tile.gridX}, {"z", tile.gridZ},
                {"height", tile.elevationLevel}, {"surface", tile.surface},
                {"shape", tile.shape}, {"occupied", tile.sourceOccupied || tile.authored}});
        }
        return tiles;
    };
    result["source_terrain_tiles"] = tileSnapshot();
    ph::AuthoredSceneDocument editorScene;
    if (!ph::loadAuthoredSceneDocument(store, std::string(game::runtime::route1_scene_variants::kRoute1.authoredSceneDocumentPath), editorScene, &error) ||
        !environment.applyAuthoredScene(editorScene, store, false, &error)) return false;
    result["editor_terrain_tiles"] = tileSnapshot();
    ph::SceneArchiveStore archive;
    game::assets::published_environment::CanonicalScene canonical;
    if (!archive.load(store, env::kCookedSceneArchivePath, &error) ||
        !game::assets::published_environment::loadCanonicalScene(
            archive, env::cookedCanonicalRoot(archive), canonical, &error)) return false;
    namespace trees = game::runtime::route1_tree_instances;
    for (const auto& mesh : canonical.meshes) {
        const auto count = trees::expectedInstanceCount(mesh.sourceIndex);
        if (!count) continue;
        trees::MeshPartition partition;
        if (!trees::derivePartition(mesh, count, partition, &error)) return false;
        Json tree{{"source_mesh_index", mesh.sourceIndex},
            {"pivot_cm", partition.sourcePivotsCm.front()}, {"groups", Json::array()}};
        for (const auto& group : partition.polygonGroups) {
            std::vector<std::uint32_t> indices;
            if (!trees::selectInstanceTriangles(mesh, group, 0, indices, &error)) return false;
            tree["groups"].push_back({{"material_index", group.materialIndex}, {"indices", indices}});
        }
        result["trees"].push_back(std::move(tree));
    }
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    std::ofstream stream(output);
    stream << result.dump(2) << '\n';
    if (!stream) { error = "Could not write authoring kit: " + output.string(); return false; }
    std::cout << "[AuthoringKit] PASS objects=" << result["objects"].size()
              << " tree_families=" << result["trees"].size() << '\n';
    return true;
}

bool validateScene(const std::string& path, std::string& error) {
    game::assets::DevAssetStore store(".");
    env::RuntimeEnvironment environment;
    ph::AuthoredSceneDocument scene;
    if (!env::loadCookedEnvironment(store, environment, nullptr, &error) ||
        !ph::loadAuthoredSceneDocument(store, path, scene, &error)) return false;
    const auto* variant = game::runtime::route1_scene_variants::find(scene.sceneId);
    if (!variant) { error = "Unknown scene variant"; return false; }
    env::BoardLayoutTransform board;
    if (!env::loadBoardLayoutTransform(store, std::string(variant->boardLayoutManifestPath), board, &error) ||
        !environment.applyBoardLayout(board, &error) ||
        !environment.applyAuthoredScene(scene, store, &error)) return false;
    std::size_t authored = 0, importedVisible = 0;
    for (const auto& object : environment.layoutObjects()) {
        if (!object.suppressed) {
            if (object.authored) ++authored;
            else if (object.targetKind != "gameplay_board_ground_prototype") ++importedVisible;
        }
    }
    if (!variant->usesSourceTerrain &&
        (importedVisible != 0 || authored == 0 || !environment.terrainTiles().empty())) {
        error = "Mesh-authored arena contains visible source objects, regenerated source tiles, or no authored geometry.";
        return false;
    }
    std::cout << "[AuthoringScene] PASS scene=" << scene.sceneId
              << " authored_visible=" << authored << " imported_visible=" << importedVisible
              << " triangles=" << environment.stats().visibleTriangleCount << '\n';
    return true;
}
}
