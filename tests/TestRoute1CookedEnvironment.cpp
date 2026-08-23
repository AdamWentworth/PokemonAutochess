#include "engine/core/IAssetStore.h"
#include "engine/core/Paths.h"
#include "game/assets/DevAssetStore.h"
#include "game/render/environment/Route1FieldEncounterGrassMaterial.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

std::array<double, 3> transformPoint(
    const std::array<float, 16>& matrix,
    const std::array<double, 3>& point) {
    return {
        static_cast<double>(matrix[0]) * point[0] +
            static_cast<double>(matrix[4]) * point[1] +
            static_cast<double>(matrix[8]) * point[2] +
            static_cast<double>(matrix[12]),
        static_cast<double>(matrix[1]) * point[0] +
            static_cast<double>(matrix[5]) * point[1] +
            static_cast<double>(matrix[9]) * point[2] +
            static_cast<double>(matrix[13]),
        static_cast<double>(matrix[2]) * point[0] +
            static_cast<double>(matrix[6]) * point[1] +
            static_cast<double>(matrix[10]) * point[2] +
            static_cast<double>(matrix[14])};
}

class CookedSceneOnlyStore final : public engine::IAssetStore {
public:
    explicit CookedSceneOnlyStore(
        std::vector<std::uint8_t> sceneBytes)
        : sceneBytes_(std::move(sceneBytes)) {}

    bool readText(
        const std::string& virtualPath,
        std::string&,
        std::string* outError) const override {
        unexpectedReads_.push_back(virtualPath);
        if (outError) {
            *outError = "Cooked-scene host does not expose loose text assets.";
        }
        return false;
    }

    bool readBytes(
        const std::string& virtualPath,
        std::vector<std::uint8_t>& outBytes,
        std::string* outError) const override {
        if (virtualPath !=
            game::runtime::route1_environment::
                kCookedSceneArchivePath) {
            unexpectedReads_.push_back(virtualPath);
            if (outError) {
                *outError =
                    "Cooked-scene host rejected loose asset: " +
                    virtualPath;
            }
            return false;
        }
        ++sceneReads_;
        if (sceneBytes_.empty()) {
            if (outError) {
                *outError = "Cooked Route 1 PHSC is unavailable.";
            }
            return false;
        }
        outBytes = sceneBytes_;
        return true;
    }

    bool exists(const std::string& virtualPath) const override {
        return virtualPath ==
                   game::runtime::route1_environment::
                       kCookedSceneArchivePath &&
            !sceneBytes_.empty();
    }

    std::size_t sceneReads() const noexcept {
        return sceneReads_;
    }

    const std::vector<std::string>& unexpectedReads() const noexcept {
        return unexpectedReads_;
    }

private:
    std::vector<std::uint8_t> sceneBytes_;
    mutable std::size_t sceneReads_ = 0u;
    mutable std::vector<std::string> unexpectedReads_;
};

} // namespace

bool test_route1_cooked_environment_contract(std::string& outFail) {
    namespace route1 =
        game::runtime::route1_environment;

    game::assets::DevAssetStore workspace(
        engine::paths::dataRoot());
    std::vector<std::uint8_t> sceneBytes;
    std::string error;
    if (!workspace.readBytes(
            route1::kCookedSceneArchivePath,
            sceneBytes,
            &error) ||
        sceneBytes.empty()) {
        outFail =
            "Route 1 cooked-environment fixture is unavailable: " +
            error;
        return false;
    }

    CookedSceneOnlyStore isolatedHost(std::move(sceneBytes));
    route1::RuntimeEnvironment environment;
    std::size_t virtualFileCount = 0u;
    if (!route1::loadCookedEnvironment(
            isolatedHost,
            environment,
            &virtualFileCount,
            &error)) {
        outFail =
            "Route 1 PHSC did not load with every loose/source-cache "
            "read denied: " + error;
        return false;
    }

    const route1::RuntimeStats& stats = environment.stats();
    if (!environment.loaded() ||
        virtualFileCount == 0u ||
        stats.sceneCount == 0u ||
        stats.materialCount == 0u ||
        stats.visibleTriangleCount == 0u) {
        outFail =
            "Route 1 PHSC mounted without a complete renderable runtime "
            "environment.";
        return false;
    }
    const std::uint32_t sourceEncounterGrassCount =
        stats.encounterGrassInstanceCount;
    const std::uint32_t sourceEncounterGrassClusterCount =
        stats.encounterGrassClusterCount;
    if (isolatedHost.sceneReads() != 1u ||
        !isolatedHost.unexpectedReads().empty()) {
        outFail =
            "Route 1 cooked startup escaped the PHSC boundary and "
            "requested a loose/source-cache asset.";
        return false;
    }

    // Existing PHSC archives retain the source-era profile identifier while
    // project-owned board manifests use the neutral publication identifier.
    // Live editor preview must accept that same compatibility pair already
    // accepted by committed layout application.
    route1::BoardLayoutTransform neutralPreview = environment.layout();
    neutralPreview.sourceProfileId =
        "route1_environment_road001_00";
    error.clear();
    if (!environment.previewBoardLayout(neutralPreview, &error)) {
        outFail =
            "A neutral project-owned Route 1 layout could not preview "
            "against the compatible legacy cooked scene profile: " +
            error;
        return false;
    }

    const auto sourceTile = std::find_if(
        environment.terrainTiles().begin(),
        environment.terrainTiles().end(),
        [](const route1::TerrainTileState& tile) {
            return tile.gridX == 25 && tile.gridZ == -14;
        });
    if (sourceTile == environment.terrainTiles().end()) {
        outFail =
            "The cooked Route 1 fixture lost terrain cell (25, -14).";
        return false;
    }
    route1::BoardLayoutTransform shadowlessLayout =
        environment.layout();
    shadowlessLayout.authoredTerrainTiles.push_back(
        route1::AuthoredTerrainTile{
            .stableId = route1::route1TerrainTileStableId(25, -14),
            .displayName = "Terrain Tile (25, -14)",
            .categoryPath = "Environment/Terrain/Tiles",
            .tileSetAssetId = "route1/terrain_tileset",
            .gridX = 25,
            .gridZ = -14,
            .elevationLevel = sourceTile->elevationLevel,
            .surface = sourceTile->surface,
            .shape = sourceTile->shape,
            .visualVariant = sourceTile->visualVariant,
            .receivesProjectedShadow = false,
            .normalizeSourceTint = true,
            .suppressOverlappingVegetation = true,
            .reason = "terrain_shadow_receiver_authoring"});
    if (!environment.applyBoardLayout(shadowlessLayout, &error)) {
        outFail =
            "A valid shadowless authored terrain cell was rejected: " +
            error;
        return false;
    }
    const auto normalizedTile = std::find_if(
        environment.terrainTiles().begin(),
        environment.terrainTiles().end(),
        [](const route1::TerrainTileState& tile) {
            return tile.gridX == 25 && tile.gridZ == -14;
        });
    if (normalizedTile == environment.terrainTiles().end() ||
        !normalizedTile->normalizeSourceTint ||
        !normalizedTile->suppressOverlappingVegetation ||
        !normalizedTile->cleanSuppressedEncounterGrassTint) {
        outFail =
            "Cell-scoped encounter-grass removal did not retain its mask or activate the clean lawn Color0 path.";
        return false;
    }
    if (sourceEncounterGrassCount == 0u ||
        sourceEncounterGrassClusterCount == 0u ||
        environment.stats().encounterGrassClusterCount + 9u !=
            sourceEncounterGrassClusterCount) {
        outFail =
            "Suppressing terrain cell (25, -14) should remove exactly the nine blade clusters rooted in that cell without deleting neighboring portions of their source modules (before=" +
            std::to_string(sourceEncounterGrassClusterCount) +
            ", after=" +
            std::to_string(
                environment.stats().encounterGrassClusterCount) +
            ").";
        return false;
    }

    const std::uint32_t clusterCountBeforeSouthStripe =
        environment.stats().encounterGrassClusterCount;
    route1::BoardLayoutTransform southStripeLayout =
        environment.layout();
    for (std::int32_t gridZ = -3; gridZ <= -1; ++gridZ) {
        const auto stripeTile = std::find_if(
            environment.terrainTiles().begin(),
            environment.terrainTiles().end(),
            [&](const route1::TerrainTileState& tile) {
                return tile.gridX == 19 && tile.gridZ == gridZ;
            });
        if (stripeTile == environment.terrainTiles().end()) {
            outFail =
                "The cooked Route 1 fixture lost an encounter-grass regression cell in the (19,-3) through (19,-1) stripe.";
            return false;
        }
        southStripeLayout.authoredTerrainTiles.push_back(
            route1::AuthoredTerrainTile{
                .stableId =
                    route1::route1TerrainTileStableId(19, gridZ),
                .displayName =
                    "Terrain Tile (19, " +
                    std::to_string(gridZ) + ")",
                .categoryPath = "Environment/Terrain/Tiles",
                .tileSetAssetId = "route1/terrain_tileset",
                .gridX = 19,
                .gridZ = gridZ,
                .elevationLevel = stripeTile->elevationLevel,
                .surface = stripeTile->surface,
                .shape = stripeTile->shape,
                .visualVariant = stripeTile->visualVariant,
                .normalizeSourceTint = true,
                .suppressOverlappingVegetation = true,
                .reason = "terrain_encounter_grass_suppression"});
    }
    if (!environment.applyBoardLayout(southStripeLayout, &error) ||
        environment.stats().encounterGrassClusterCount + 29u !=
            clusterCountBeforeSouthStripe) {
        outFail =
            "Cells (19,-3) through (19,-1) should remove all 29 source-weighted blade clusters visually rooted in those cells: " +
            error + " (before=" +
            std::to_string(clusterCountBeforeSouthStripe) +
            ", after=" +
            std::to_string(
                environment.stats().encounterGrassClusterCount) +
            ").";
        return false;
    }
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        batches;
    environment.appendIndexedBatches(0.0f, batches);
    const std::set<std::pair<std::int32_t, std::int32_t>>
        suppressedCells{{25, -14}, {19, -3}, {19, -2}, {19, -1}};
    const auto sourceFromWorld =
        route1::sourceFromWorldMatrix(environment.layout());
    std::size_t verifiedGrassJointCount = 0u;
    for (const auto& batch : batches) {
        const auto& material =
            game::runtime::shared_world_batches::
                resolvedMaterialBatch(batch);
        if (material.materialMode !=
            engine::render::route1_field_encounter_grass::
                kMaterialMode) {
            continue;
        }
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const std::size_t vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        constexpr std::size_t kMaximumTestJointCount = 16u;
        std::array<std::array<double, 3>, kMaximumTestJointCount>
            weightedPositions{};
        std::array<double, kMaximumTestJointCount> totalWeights{};
        for (std::size_t vertexIndex = 0u;
             vertexIndex < vertexCount;
             ++vertexIndex) {
            const auto& vertex = vertices[vertexIndex];
            const std::array<float, 4> joints{
                vertex.joint0,
                vertex.joint1,
                vertex.joint2,
                vertex.joint3};
            const std::array<float, 4> weights{
                vertex.weight0,
                vertex.weight1,
                vertex.weight2,
                vertex.weight3};
            for (std::size_t influence = 0u;
                 influence < joints.size();
                 ++influence) {
                const auto joint = static_cast<std::int32_t>(
                    std::lround(joints[influence]));
                if (joint <= 0 ||
                    static_cast<std::size_t>(joint) >=
                        kMaximumTestJointCount ||
                    weights[influence] <= 0.0f ||
                    std::abs(
                        joints[influence] -
                        static_cast<float>(joint)) > 0.001f) {
                    continue;
                }
                const std::size_t jointIndex =
                    static_cast<std::size_t>(joint);
                weightedPositions[jointIndex][0] +=
                    static_cast<double>(vertex.x) * weights[influence];
                weightedPositions[jointIndex][1] +=
                    static_cast<double>(vertex.y) * weights[influence];
                weightedPositions[jointIndex][2] +=
                    static_cast<double>(vertex.z) * weights[influence];
                totalWeights[jointIndex] += weights[influence];
            }
        }
        for (const auto& instance : batch.instances) {
            if (instance.gpuSkinning == 0u ||
                !instance.skinMatrices) {
                continue;
            }
            for (std::uint32_t joint = 1u;
                 joint < instance.skinMatrixCount &&
                     joint < kMaximumTestJointCount;
                 ++joint) {
                const std::size_t jointIndex =
                    static_cast<std::size_t>(joint);
                if (totalWeights[jointIndex] <= 0.0) {
                    continue;
                }
                const std::array<double, 3> localAnchor{
                    weightedPositions[jointIndex][0] /
                        totalWeights[jointIndex],
                    weightedPositions[jointIndex][1] /
                        totalWeights[jointIndex],
                    weightedPositions[jointIndex][2] /
                        totalWeights[jointIndex]};
                const auto worldAnchor = transformPoint(
                    instance.modelMatrix,
                    localAnchor);
                const auto sourceAnchor = transformPoint(
                    sourceFromWorld,
                    worldAnchor);
                const std::pair<std::int32_t, std::int32_t> cell{
                    static_cast<std::int32_t>(
                        std::floor(sourceAnchor[0] / 100.0)),
                    static_cast<std::int32_t>(
                        std::floor(sourceAnchor[2] / 100.0))};
                const bool shouldBeHidden =
                    suppressedCells.contains(cell);
                const bool isHidden = std::abs(
                    instance.skinMatrices[jointIndex * 16u + 13u] +
                    10000.0f) < 0.001f;
                if (isHidden != shouldBeHidden) {
                    outFail =
                        "Encounter-grass GPU masking disagrees with the rendered weighted-vertex cell for joint " +
                        std::to_string(joint) + " at (" +
                        std::to_string(cell.first) + "," +
                        std::to_string(cell.second) + ").";
                    return false;
                }
                ++verifiedGrassJointCount;
            }
        }
    }
    if (verifiedGrassJointCount == 0u) {
        outFail =
            "Cell-scoped encounter-grass suppression did not expose any source-weighted GPU skin joints for spatial verification.";
        return false;
    }

    const auto shadowlessBatch = std::find_if(
        batches.begin(),
        batches.end(),
        [](const auto& batch) {
            return batch.geometryCacheKey.find(
                       ":shadowless") != std::string::npos;
        });
    if (shadowlessBatch == batches.end() ||
        game::runtime::shared_world_batches::resolvedMaterialBatch(
            *shadowlessBatch).projectedShadowEnabled != 0u) {
        outFail =
            "Authored shadowless terrain did not receive a dedicated material with projected shadows disabled.";
        return false;
    }
    const auto* normalizedVertices = shadowlessBatch->sharedVertices
        ? shadowlessBatch->sharedVertices
        : shadowlessBatch->vertices.data();
    const std::size_t normalizedVertexCount =
        shadowlessBatch->sharedVertices
        ? shadowlessBatch->sharedVertexCount
        : shadowlessBatch->vertices.size();
    const auto findVertex = [&](float x, float z) {
        const engine::render::backend::WorldMeshVertex* closest = nullptr;
        float closestDistanceSquared = 4.0f;
        for (std::size_t index = 0u;
             index < normalizedVertexCount;
             ++index) {
            const float deltaX = normalizedVertices[index].x - x;
            const float deltaZ = normalizedVertices[index].z - z;
            const float distanceSquared =
                deltaX * deltaX + deltaZ * deltaZ;
            if (distanceSquared < closestDistanceSquared) {
                closest = normalizedVertices + index;
                closestDistanceSquared = distanceSquared;
            }
        }
        return closest;
    };
    const auto* cleanInterior = findVertex(2550.0f, -1350.0f);
    if (!cleanInterior ||
        std::abs(cleanInterior->r - 1.0f) > 0.001f ||
        std::abs(cleanInterior->g - 1.0f) > 0.001f ||
        std::abs(cleanInterior->b - 1.0f) > 0.001f) {
        outFail =
            "Normalized lawn did not retain clean Color0 in its interior: " +
            (cleanInterior
                 ? std::to_string(cleanInterior->r) + "," +
                       std::to_string(cleanInterior->g) + "," +
                       std::to_string(cleanInterior->b)
                 : std::string("missing"));
        return false;
    }

    route1::BoardLayoutTransform loweredLawnLayout =
        environment.layout();
    const auto authoredTileFromSource =
        [&](std::int32_t gridX,
            std::int32_t gridZ,
            std::int32_t elevationLevel,
            std::string surface,
            std::string visualVariant) {
            const auto source = std::find_if(
                environment.terrainTiles().begin(),
                environment.terrainTiles().end(),
                [&](const route1::TerrainTileState& tile) {
                    return tile.gridX == gridX && tile.gridZ == gridZ;
                });
            route1::AuthoredTerrainTile tile;
            if (source == environment.terrainTiles().end()) {
                return tile;
            }
            tile.stableId = route1::route1TerrainTileStableId(
                gridX, gridZ);
            tile.displayName = "Terrain Tile (" +
                std::to_string(gridX) + ", " +
                std::to_string(gridZ) + ")";
            tile.categoryPath = "Environment/Terrain/Tiles";
            tile.tileSetAssetId = "route1/terrain_tileset";
            tile.gridX = gridX;
            tile.gridZ = gridZ;
            tile.elevationLevel = elevationLevel;
            tile.surface = std::move(surface);
            tile.shape = "flat";
            tile.visualVariant = std::move(visualVariant);
            tile.receivesProjectedShadow = true;
            tile.reason = "terrain_lawn_detail_regression";
            return tile;
        };
    loweredLawnLayout.authoredTerrainTiles.push_back(
        authoredTileFromSource(17, -1, 0, "light_lawn", "auto"));
    loweredLawnLayout.authoredTerrainTiles.push_back(
        authoredTileFromSource(17, -2, 0, "dirt_path", "path_2"));
    if (!environment.applyBoardLayout(loweredLawnLayout, &error)) {
        outFail =
            "A lowered source light-lawn cell beside an authored dirt path was rejected: " +
            error;
        return false;
    }
    const auto loweredLawn = std::find_if(
        environment.terrainTiles().begin(),
        environment.terrainTiles().end(),
        [](const route1::TerrainTileState& tile) {
            return tile.gridX == 17 && tile.gridZ == -1;
        });
    const auto retainedDirtPath = std::find_if(
        environment.terrainTiles().begin(),
        environment.terrainTiles().end(),
        [](const route1::TerrainTileState& tile) {
            return tile.gridX == 17 && tile.gridZ == -2;
        });
    if (loweredLawn == environment.terrainTiles().end() ||
        loweredLawn->sourceElevationLevel != 1 ||
        loweredLawn->elevationLevel != 0 ||
        loweredLawn->sourceSurface != "light_lawn" ||
        loweredLawn->surface != "light_lawn" ||
        loweredLawn->normalizeSourceTint ||
        retainedDirtPath == environment.terrainTiles().end() ||
        retainedDirtPath->surface != "dirt_path" ||
        retainedDirtPath->visualVariant != "path_2") {
        outFail =
            "The lowered-lawn regression fixture did not retain its independent source-lawn and authored dirt-path states.";
        return false;
    }
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        loweredLawnBatches;
    environment.appendIndexedBatches(0.0f, loweredLawnBatches);
    float minimumLawnUv2U = std::numeric_limits<float>::max();
    float maximumLawnUv2U = std::numeric_limits<float>::lowest();
    float minimumLawnUv2V = std::numeric_limits<float>::max();
    float maximumLawnUv2V = std::numeric_limits<float>::lowest();
    std::size_t loweredLawnVertexCount = 0u;
    for (const auto& batch : loweredLawnBatches) {
        if (batch.geometryCacheKey.find(
                "route1:terrain-authored-surface:") ==
            std::string::npos) {
            continue;
        }
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const std::size_t vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        for (std::size_t vertexIndex = 0u;
             vertexIndex < vertexCount;
             ++vertexIndex) {
            const auto& vertex = vertices[vertexIndex];
            if (vertex.x < 1700.0f - 0.01f ||
                vertex.x > 1800.0f + 0.01f ||
                vertex.z < -100.0f - 0.01f ||
                vertex.z > 0.0f + 0.01f) {
                continue;
            }
            minimumLawnUv2U = std::min(
                minimumLawnUv2U, vertex.sourceUv2U);
            maximumLawnUv2U = std::max(
                maximumLawnUv2U, vertex.sourceUv2U);
            minimumLawnUv2V = std::min(
                minimumLawnUv2V, vertex.sourceUv2V);
            maximumLawnUv2V = std::max(
                maximumLawnUv2V, vertex.sourceUv2V);
            ++loweredLawnVertexCount;
        }
    }
    if (loweredLawnVertexCount == 0u ||
        (maximumLawnUv2U - minimumLawnUv2U < 0.001f &&
         maximumLawnUv2V - minimumLawnUv2V < 0.001f)) {
        outFail =
            "Lowering a source light-lawn cell flattened its recovered leafy UV2 field to one plain sample.";
        return false;
    }
    namespace variants =
        game::runtime::route1_scene_variants;
    const std::array variantCases{
        std::pair{&variants::kRoute1,
                  std::array<std::int32_t, 2>{17, -10}},
        std::pair{&variants::kRoute1_5,
                  std::array<std::int32_t, 2>{17, -19}}};
    for (const auto& [variant, expectedOrigin] : variantCases) {
        route1::RuntimeEnvironment variantEnvironment;
        route1::BoardLayoutTransform variantLayout;
        engine::assets::phlosion::AuthoredSceneDocument
            variantScene;
        if (!route1::loadCookedEnvironment(
                workspace,
                variantEnvironment,
                nullptr,
                &error) ||
            !route1::loadBoardLayoutTransform(
                workspace,
                std::string(variant->boardLayoutManifestPath),
                variantLayout,
                &error) ||
            !variantEnvironment.applyBoardLayout(
                variantLayout,
                &error) ||
            !engine::assets::phlosion::loadAuthoredSceneDocument(
                workspace,
                std::string(variant->authoredSceneDocumentPath),
                variantScene,
                &error) ||
            !variantEnvironment.applyAuthoredScene(
                variantScene,
                &error)) {
            outFail =
                "Route 1 scene variant could not compose independently: " +
                std::string(variant->sceneId) + ": " + error;
            return false;
        }
        const bool requiresPinnedAuthoredLayout =
            variant == &variants::kRoute1_5;
        if (variantScene.sceneId != variant->sceneId ||
            variantEnvironment.layout().terrainGridOrigin !=
                expectedOrigin ||
            (requiresPinnedAuthoredLayout &&
             variantScene.nodes.empty())) {
            outFail =
                "Route 1 scene variant lost its independent identity, board "
                "registration, or pinned authored layout: " +
                std::string(variant->sceneId);
            return false;
        }
    }

    CookedSceneOnlyStore missingHost({});
    virtualFileCount = 99u;
    error.clear();
    if (route1::loadCookedEnvironment(
            missingHost,
            environment,
            &virtualFileCount,
            &error) ||
        virtualFileCount != 0u ||
        error.find("PHSC") == std::string::npos ||
        !environment.loaded()) {
        outFail =
            "Missing Route 1 PHSC should fail actionably and preserve the "
            "last valid runtime environment.";
        return false;
    }

    return true;
}
