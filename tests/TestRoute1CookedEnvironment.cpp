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
    const auto sourceFromWorld =
        route1::sourceFromWorldMatrix(environment.layout());
    bool foundFilteredGrassClusterGeometry = false;
    std::size_t verifiedGrassVertexCount = 0u;
    for (const auto& batch : batches) {
        const auto& material =
            game::runtime::shared_world_batches::
                resolvedMaterialBatch(batch);
        if (material.materialMode !=
            engine::render::route1_field_encounter_grass::
                kMaterialMode) {
            continue;
        }
        foundFilteredGrassClusterGeometry =
            foundFilteredGrassClusterGeometry ||
            batch.geometryCacheKey.find(
                ":encounter-cluster-mask:visible:") !=
                std::string::npos;
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const std::size_t vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        for (const auto& instance : batch.instances) {
            if (instance.gpuSkinning == 0u ||
                !instance.skinMatrices) {
                continue;
            }
            for (std::uint32_t joint = 0u;
                 joint < instance.skinMatrixCount;
                 ++joint) {
                if (instance.skinMatrices[
                        static_cast<std::size_t>(joint) * 16u + 13u] <
                    -1000.0f) {
                    outFail =
                        "Encounter-grass masking still hides a joint by translating it far below the terrain.";
                    return false;
                }
            }
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
                std::array<double, 3> skinned{};
                double totalWeight = 0.0;
                for (std::size_t influence = 0u;
                     influence < joints.size();
                     ++influence) {
                    const auto joint = static_cast<std::int32_t>(
                        std::lround(joints[influence]));
                    if (joint < 0 ||
                        static_cast<std::uint32_t>(joint) >=
                            instance.skinMatrixCount ||
                        weights[influence] <= 0.0f ||
                        std::abs(
                            joints[influence] -
                            static_cast<float>(joint)) > 0.001f) {
                        continue;
                    }
                    std::array<float, 16> jointMatrix{};
                    std::copy_n(
                        instance.skinMatrices +
                            static_cast<std::size_t>(joint) * 16u,
                        jointMatrix.size(),
                        jointMatrix.begin());
                    const auto transformed = transformPoint(
                        jointMatrix,
                        {vertex.x, vertex.y, vertex.z});
                    for (std::size_t axis = 0u; axis < 3u; ++axis) {
                        skinned[axis] += transformed[axis] *
                            static_cast<double>(weights[influence]);
                    }
                    totalWeight += weights[influence];
                }
                if (totalWeight <= 0.0) {
                    skinned = {vertex.x, vertex.y, vertex.z};
                }
                const auto worldPosition = transformPoint(
                    instance.modelMatrix, skinned);
                const auto sourcePosition = transformPoint(
                    sourceFromWorld, worldPosition);
                if (sourcePosition[1] < -100.0) {
                    outFail =
                        "Encounter-grass cell masking stretched submitted geometry outside the source vegetation height band: y=" +
                        std::to_string(sourcePosition[1]) +
                        " key=" + batch.geometryCacheKey + ".";
                    return false;
                }
                ++verifiedGrassVertexCount;
            }
        }
    }
    if (!foundFilteredGrassClusterGeometry ||
        verifiedGrassVertexCount == 0u) {
        outFail =
            "Cell-scoped encounter-grass suppression did not submit filtered cluster geometry for verification.";
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
    const auto validReconstructedChannel = [](float channel) {
        return std::isfinite(channel) &&
            channel >= 0.0f && channel <= 1.0f;
    };
    if (!cleanInterior ||
        !validReconstructedChannel(cleanInterior->r) ||
        !validReconstructedChannel(cleanInterior->g) ||
        !validReconstructedChannel(cleanInterior->b) ||
        (std::abs(cleanInterior->r - 1.0f) <= 0.001f &&
         std::abs(cleanInterior->g - 1.0f) <= 0.001f &&
         std::abs(cleanInterior->b - 1.0f) <= 0.001f)) {
        outFail =
            "Normalized lawn did not reconstruct a valid donor-driven Color0 in its interior: " +
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
        authoredTileFromSource(16, -1, 1, "light_lawn", "auto"));
    loweredLawnLayout.authoredTerrainTiles.push_back(
        authoredTileFromSource(17, -2, 0, "dirt_path", "path_2"));
    auto westOrdinaryLawn =
        authoredTileFromSource(18, -1, 0, "light_lawn", "auto");
    westOrdinaryLawn.receivesProjectedShadow = false;
    loweredLawnLayout.authoredTerrainTiles.push_back(
        std::move(westOrdinaryLawn));
    std::erase_if(
        loweredLawnLayout.authoredTerrainTiles,
        [](const route1::AuthoredTerrainTile& tile) {
            return tile.gridZ == -1 &&
                tile.gridX >= 19 && tile.gridX <= 21;
        });
    for (std::int32_t gridX = 19; gridX <= 21; ++gridX) {
        auto normalizedLawn = authoredTileFromSource(
            gridX, -1, 0, "light_lawn", "auto");
        normalizedLawn.normalizeSourceTint = true;
        normalizedLawn.suppressOverlappingVegetation = true;
        loweredLawnLayout.authoredTerrainTiles.push_back(
            std::move(normalizedLawn));
    }
    for (std::int32_t gridZ = -4; gridZ <= -1; ++gridZ) {
        auto eastOrdinaryTile = authoredTileFromSource(
            22,
            gridZ,
            0,
            gridZ == -2 ? "dirt_path" : "light_lawn",
            gridZ == -2 ? "path_10" : "auto");
        eastOrdinaryTile.receivesProjectedShadow = false;
        loweredLawnLayout.authoredTerrainTiles.push_back(
            std::move(eastOrdinaryTile));
    }
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
    std::vector<std::array<float, 3>> formerLedgeUv2Samples;
    float minimumLawnUv2U = std::numeric_limits<float>::max();
    float maximumLawnUv2U = std::numeric_limits<float>::lowest();
    float minimumLawnUv2V = std::numeric_limits<float>::max();
    float maximumLawnUv2V = std::numeric_limits<float>::lowest();
    std::size_t loweredLawnVertexCount = 0u;
    std::size_t rebuiltFormerLedgeVertexCount = 0u;
    struct BoundaryVertexRange {
        std::array<float, 4> minimum{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        std::array<float, 4> maximum{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
        std::size_t count = 0u;
        std::array<float, 7> attributeMinimum{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        std::array<float, 7> attributeMaximum{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
    };
    BoundaryVertexRange westLawnBoundary;
    BoundaryVertexRange eastLawnBoundary;
    bool foundSourceFaithfulCliffBands = false;
    bool foundContinuousFringeField = false;
    bool foundConcaveCliffTrim = false;
    bool foundConcaveFringeTrim = false;
    bool foundFringeCorner = false;
    bool foundCliffCorner = false;
    bool foundInsetOrganicCliff = false;
    bool foundInsetOrganicFringe = false;
    bool foundFringeCrownOverlap = false;
    bool foundLowerLawnLedgeOverlap = false;
    bool foundUpperLawnCrownClip = false;
    float maximumUpperLawnCrownX =
        std::numeric_limits<float>::lowest();
    float minimumFullCliffCrownOutward =
        std::numeric_limits<float>::max();
    float maximumFullCliffCrownOutward =
        std::numeric_limits<float>::lowest();
    float minimumFullFringeCrownOutward =
        std::numeric_limits<float>::max();
    float maximumFullFringeCrownOutward =
        std::numeric_limits<float>::lowest();
    for (const auto& batch : loweredLawnBatches) {
        if (batch.geometryCacheKey.find(
                "route1:terrain-cliff-corner:") !=
            std::string::npos) {
            const auto* vertices = batch.sharedVertices
                ? batch.sharedVertices
                : batch.vertices.data();
            const std::size_t vertexCount = batch.sharedVertices
                ? batch.sharedVertexCount
                : batch.vertices.size();
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                if (std::abs(vertices[vertexIndex].x) > 50.001f ||
                    std::abs(vertices[vertexIndex].z) > 50.001f) {
                    outFail =
                        "A rebuilt Route 1 cliff corner exceeded its owning tile footprint: " +
                        batch.geometryCacheKey;
                    return false;
                }
            }
            foundCliffCorner = true;
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-fringe-corner:") !=
            std::string::npos) {
            const auto* vertices = batch.sharedVertices
                ? batch.sharedVertices
                : batch.vertices.data();
            const std::size_t vertexCount = batch.sharedVertices
                ? batch.sharedVertexCount
                : batch.vertices.size();
            const std::size_t indexCount = batch.sharedIndices
                ? batch.sharedIndexCount
                : batch.indices.size();
            if (vertexCount != 15u || indexCount != 48u) {
                outFail =
                    "A rebuilt convex Route 1 corner did not submit the complete three-row leafy quarter-arc: " +
                    batch.geometryCacheKey;
                return false;
            }
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                if (std::abs(vertices[vertexIndex].x) > 50.001f ||
                    std::abs(vertices[vertexIndex].z) > 50.001f) {
                    outFail =
                        "A rebuilt Route 1 leafy corner exceeded its owning tile footprint: " +
                        batch.geometryCacheKey;
                    return false;
                }
            }
            foundFringeCorner = true;
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-cliff:") != std::string::npos &&
            batch.geometryCacheKey.find(
                ":tile-levels-1-1:neighbor-levels-0-0:") !=
                std::string::npos) {
            const auto* vertices = batch.sharedVertices
                ? batch.sharedVertices
                : batch.vertices.data();
            const std::size_t vertexCount = batch.sharedVertices
                ? batch.sharedVertexCount
                : batch.vertices.size();
            const std::size_t indexCount = batch.sharedIndices
                ? batch.sharedIndexCount
                : batch.indices.size();
            float minimumY = std::numeric_limits<float>::max();
            float maximumY = std::numeric_limits<float>::lowest();
            bool foundLowerTint = false;
            bool foundUpperWhite = false;
            bool foundAdvancingLowerMask = false;
            bool foundNeutralUpperMask = false;
            float minimumFootOutward =
                std::numeric_limits<float>::max();
            float maximumFootOutward =
                std::numeric_limits<float>::lowest();
            float minimumCrownOutward =
                std::numeric_limits<float>::max();
            float maximumCrownOutward =
                std::numeric_limits<float>::lowest();
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                const auto& vertex = vertices[vertexIndex];
                minimumY = std::min(minimumY, vertex.y);
                maximumY = std::max(maximumY, vertex.y);
                foundLowerTint = foundLowerTint ||
                    (vertex.sourceUv1V <= -0.013f &&
                     std::abs(vertex.r - 0.180392161f) <= 0.001f &&
                     std::abs(vertex.g - 0.482352942f) <= 0.001f &&
                     std::abs(vertex.b - 0.431372553f) <= 0.001f);
                foundUpperWhite = foundUpperWhite ||
                    (vertex.sourceUv1V >= 0.686f &&
                     std::abs(vertex.r - 1.0f) <= 0.001f &&
                     std::abs(vertex.g - 1.0f) <= 0.001f &&
                     std::abs(vertex.b - 1.0f) <= 0.001f);
                foundAdvancingLowerMask =
                    foundAdvancingLowerMask ||
                    (vertex.sourceUv1V <= 0.3231f &&
                     std::abs(vertex.sourceUv2U + 0.05f) > 0.001f);
                foundNeutralUpperMask =
                    foundNeutralUpperMask ||
                    (vertex.sourceUv1V >= 0.686f &&
                     std::abs(vertex.sourceUv2U + 0.05f) <= 0.001f &&
                     std::abs(vertex.sourceUv2V - 0.85f) <= 0.001f);
                if (vertex.sourceUv1V <= -0.013f) {
                    minimumFootOutward = std::min(
                        minimumFootOutward, vertex.z);
                    maximumFootOutward = std::max(
                        maximumFootOutward, vertex.z);
                    constexpr float kInsetEndpoint = 48.0f;
                    const bool startTrimmed =
                        batch.geometryCacheKey.find(":joins-3-") !=
                            std::string::npos &&
                        std::abs(vertex.x + kInsetEndpoint) <= 0.001f;
                    const bool endTrimmed =
                        batch.geometryCacheKey.ends_with("-3") &&
                        std::abs(vertex.x - kInsetEndpoint) <= 0.001f;
                    foundConcaveCliffTrim =
                        foundConcaveCliffTrim ||
                        startTrimmed || endTrimmed;
                }
                if (vertex.sourceUv1V >= 0.985f) {
                    minimumCrownOutward = std::min(
                        minimumCrownOutward, vertex.z);
                    maximumCrownOutward = std::max(
                        maximumCrownOutward, vertex.z);
                }
            }
            if (vertexCount != 54u || indexCount != 144u ||
                std::abs(
                    minimumY -
                    (0.32f - 0.02f)) > 0.001f ||
                std::abs(
                    maximumY -
                    (48.0f + 0.32f - 0.02f)) > 0.001f ||
                !foundLowerTint || !foundUpperWhite ||
                !foundAdvancingLowerMask ||
                !foundNeutralUpperMask) {
                outFail =
                    "A rebuilt one-level Route 1 cliff no longer preserves the source mesh's duplicated bands, 48 cm profile, lower tint, or UV2 mask transition: " +
                    batch.geometryCacheKey;
                return false;
            }
            if (maximumFootOutward > 3.0f ||
                maximumCrownOutward > -22.0f ||
                maximumFootOutward - minimumFootOutward < 1.0f ||
                maximumCrownOutward - minimumCrownOutward < 1.0f) {
                outFail =
                    "A rebuilt Route 1 cliff lost its measured inward foot/crown offsets or regressed to a ruler-straight strip: " +
                    batch.geometryCacheKey;
                return false;
            }
            foundInsetOrganicCliff = true;
            foundSourceFaithfulCliffBands = true;
            minimumFullCliffCrownOutward = std::min(
                minimumFullCliffCrownOutward,
                minimumCrownOutward);
            maximumFullCliffCrownOutward = std::max(
                maximumFullCliffCrownOutward,
                maximumCrownOutward);
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-fringe:") != std::string::npos) {
            const auto* vertices = batch.sharedVertices
                ? batch.sharedVertices
                : batch.vertices.data();
            const std::size_t vertexCount = batch.sharedVertices
                ? batch.sharedVertexCount
                : batch.vertices.size();
            if (vertexCount == 27u &&
                (batch.sharedIndices
                    ? batch.sharedIndexCount
                    : batch.indices.size()) == 96u) {
                float minimumMaskU =
                    std::numeric_limits<float>::max();
                float maximumMaskU =
                    std::numeric_limits<float>::lowest();
                bool foundGreenCrown = false;
                bool foundSlopedCarrier = false;
                float minimumCrownOutward =
                    std::numeric_limits<float>::max();
                float maximumCrownOutward =
                    std::numeric_limits<float>::lowest();
                for (std::size_t vertexIndex = 0u;
                     vertexIndex < vertexCount;
                     ++vertexIndex) {
                    const auto& vertex = vertices[vertexIndex];
                    minimumMaskU = std::min(
                        minimumMaskU,
                        vertex.sourceUv1U);
                    maximumMaskU = std::max(
                        maximumMaskU,
                        vertex.sourceUv1U);
                    foundGreenCrown = foundGreenCrown ||
                        (std::abs(
                             vertex.sourceUv1V -
                             0.993270993f) <= 0.001f &&
                         std::abs(
                             vertex.r - 0.180392161f) <= 0.001f &&
                         std::abs(
                             vertex.g - 0.482352942f) <= 0.001f &&
                         std::abs(
                             vertex.b - 0.431372553f) <= 0.001f);
                    foundSlopedCarrier = foundSlopedCarrier ||
                        std::abs(
                            vertex.sourceUv1V -
                            0.789638996f) <= 0.001f;
                    if (std::abs(
                            vertex.sourceUv1V -
                            0.993270993f) <= 0.001f) {
                        minimumCrownOutward = std::min(
                            minimumCrownOutward, vertex.z);
                        maximumCrownOutward = std::max(
                            maximumCrownOutward, vertex.z);
                        foundFringeCrownOverlap =
                            foundFringeCrownOverlap ||
                            vertex.y >= 0.339f;
                    }
                    if (std::abs(
                            vertex.sourceUv1V -
                            0.993270993f) <= 0.001f) {
                        constexpr float kInsetEndpoint = 22.99f;
                        const bool startTrimmed =
                            batch.geometryCacheKey.find(":joins-3-") !=
                                std::string::npos &&
                            std::abs(
                                vertex.x + kInsetEndpoint) <= 0.001f;
                        const bool endTrimmed =
                            batch.geometryCacheKey.ends_with("-3") &&
                            std::abs(
                                vertex.x - kInsetEndpoint) <= 0.001f;
                        foundConcaveFringeTrim =
                            foundConcaveFringeTrim ||
                            startTrimmed || endTrimmed;
                    }
                }
                foundContinuousFringeField =
                    foundContinuousFringeField ||
                    (std::abs(
                         maximumMaskU - minimumMaskU -
                         0.546140313f) <= 0.001f &&
                     foundGreenCrown && foundSlopedCarrier);
                foundInsetOrganicFringe =
                    foundInsetOrganicFringe ||
                    (maximumCrownOutward <= -21.0f &&
                     maximumCrownOutward - minimumCrownOutward >= 1.0f);
                if (batch.geometryCacheKey.find(
                        ":tile-levels-1-1:neighbor-levels-0-0:") !=
                    std::string::npos) {
                    minimumFullFringeCrownOutward = std::min(
                        minimumFullFringeCrownOutward,
                        minimumCrownOutward);
                    maximumFullFringeCrownOutward = std::max(
                        maximumFullFringeCrownOutward,
                        maximumCrownOutward);
                }
            }
        }
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
            foundLowerLawnLedgeOverlap =
                foundLowerLawnLedgeOverlap ||
                (vertex.x >= 1697.64f && vertex.x <= 1697.66f &&
                 vertex.z >= -100.01f && vertex.z <= 0.01f &&
                 std::abs(vertex.y - 0.32f) <= 0.01f);
            foundUpperLawnCrownClip =
                foundUpperLawnCrownClip ||
                (vertex.x >= 1672.98f && vertex.x <= 1673.00f &&
                 vertex.z >= -100.01f && vertex.z <= 0.01f &&
                 std::abs(vertex.y - 50.32f) <= 0.01f);
            if (vertex.x >= 1600.0f && vertex.x <= 1700.0f &&
                vertex.z >= -100.01f && vertex.z <= 0.01f &&
                std::abs(vertex.y - 50.32f) <= 0.01f) {
                maximumUpperLawnCrownX = std::max(
                    maximumUpperLawnCrownX, vertex.x);
            }
            if (std::abs(vertex.z + 50.0f) <= 0.01f) {
                BoundaryVertexRange* boundary = nullptr;
                if (std::abs(vertex.x - 1900.0f) <= 0.01f) {
                    boundary = &westLawnBoundary;
                } else if (std::abs(vertex.x - 2200.0f) <= 0.01f) {
                    boundary = &eastLawnBoundary;
                }
                if (boundary) {
                    const std::array<float, 4> color{
                        vertex.r,
                        vertex.g,
                        vertex.b,
                        vertex.a};
                    for (std::size_t channel = 0u;
                         channel < color.size();
                         ++channel) {
                        boundary->minimum[channel] = std::min(
                            boundary->minimum[channel], color[channel]);
                        boundary->maximum[channel] = std::max(
                            boundary->maximum[channel], color[channel]);
                    }
                    const std::array<float, 7> attributes{
                        vertex.y,
                        vertex.u,
                        vertex.v,
                        vertex.sourceUv1U,
                        vertex.sourceUv1V,
                        vertex.sourceUv2U,
                        vertex.sourceUv2V};
                    for (std::size_t attribute = 0u;
                         attribute < attributes.size();
                         ++attribute) {
                        boundary->attributeMinimum[attribute] = std::min(
                            boundary->attributeMinimum[attribute],
                            attributes[attribute]);
                        boundary->attributeMaximum[attribute] = std::max(
                            boundary->attributeMaximum[attribute],
                            attributes[attribute]);
                    }
                    ++boundary->count;
                }
            }
            if (vertex.x >= 2200.01f &&
                vertex.x <= 2299.99f &&
                vertex.z >= -400.0f - 0.01f &&
                vertex.z <= 0.0f + 0.01f) {
                const float expectedU = vertex.x / 300.0f;
                const float expectedV = vertex.z / 300.0f;
                if (std::abs(vertex.u - expectedU) > 0.001f ||
                    std::abs(vertex.v - expectedV) > 0.001f) {
                    outFail =
                        "A topology-edited x=22 floor retained UV0 from its old jagged raised mesh.";
                    return false;
                }
                ++rebuiltFormerLedgeVertexCount;
            }
            if (vertex.x >= 2200.0f - 0.01f &&
                vertex.x <= 2240.0f + 0.01f &&
                std::abs(vertex.z + 50.0f) <= 0.01f) {
                formerLedgeUv2Samples.push_back(
                    {vertex.x,
                     vertex.sourceUv2U,
                     vertex.sourceUv2V});
            }
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
    if (!foundSourceFaithfulCliffBands ||
        !foundContinuousFringeField ||
        !foundConcaveCliffTrim ||
        !foundConcaveFringeTrim ||
        !foundFringeCorner ||
        !foundCliffCorner ||
        !foundInsetOrganicCliff ||
        !foundInsetOrganicFringe ||
        !foundFringeCrownOverlap) {
        outFail =
            "Lowering Route 1 terrain did not submit the measured inset organic cliff/fringe profiles, overlap their crown with the rebuilt top, preserve concave joins, or keep both rounded corner carriers inside their owning tile (cliff-bands=" +
            std::to_string(foundSourceFaithfulCliffBands) +
            ", fringe-field=" +
            std::to_string(foundContinuousFringeField) +
            ", concave-cliff=" +
            std::to_string(foundConcaveCliffTrim) +
            ", concave-fringe=" +
            std::to_string(foundConcaveFringeTrim) +
            ", fringe-corner=" +
            std::to_string(foundFringeCorner) +
            ", cliff-corner=" +
            std::to_string(foundCliffCorner) +
            ", organic-cliff=" +
            std::to_string(foundInsetOrganicCliff) +
            ", organic-fringe=" +
            std::to_string(foundInsetOrganicFringe) +
            ", crown-overlap=" +
            std::to_string(foundFringeCrownOverlap) + ").";
        return false;
    }
    if (!foundLowerLawnLedgeOverlap) {
        outFail =
            "The rebuilt lower Route 1 lawn did not overlap beneath the inset generated cliff foot.";
        return false;
    }
    if (!foundUpperLawnCrownClip ||
        maximumUpperLawnCrownX > 1675.0f) {
        outFail =
            "The rebuilt upper Route 1 lawn was not clipped to the shared cliff/fringe crown without a square overhang (found=" +
            std::to_string(foundUpperLawnCrownClip) +
            ", maximum-x=" +
            std::to_string(maximumUpperLawnCrownX) + ").";
        return false;
    }
    if (std::abs(
            minimumFullCliffCrownOutward -
            minimumFullFringeCrownOutward) > 0.02f ||
        std::abs(
            maximumFullCliffCrownOutward -
            maximumFullFringeCrownOutward) > 0.02f) {
        outFail =
            "The rebuilt Route 1 cliff and leafy carrier no longer share one measured crown contour.";
        return false;
    }
    if (rebuiltFormerLedgeVertexCount == 0u) {
        outFail =
            "The former x=22 ledge did not expose rebuilt floor vertices.";
        return false;
    }
    const auto continuousBoundaryColor =
        [](const BoundaryVertexRange& boundary) {
            if (boundary.count < 2u) {
                return false;
            }
            for (std::size_t channel = 0u;
                 channel < boundary.minimum.size();
                 ++channel) {
                if (boundary.maximum[channel] -
                        boundary.minimum[channel] >
                    0.001f) {
                    return false;
                }
            }
            return true;
        };
    if (!continuousBoundaryColor(westLawnBoundary) ||
        !continuousBoundaryColor(eastLawnBoundary)) {
        outFail =
            "Tint-normalized and ordinary light-lawn cells did not share one Color0 value at their common edge (west count=" +
            std::to_string(westLawnBoundary.count) +
            " r=" + std::to_string(westLawnBoundary.minimum[0]) +
            ".." + std::to_string(westLawnBoundary.maximum[0]) +
            ", east count=" +
            std::to_string(eastLawnBoundary.count) +
            " r=" + std::to_string(eastLawnBoundary.minimum[0]) +
            ".." + std::to_string(eastLawnBoundary.maximum[0]) +
            ").";
        return false;
    }
    const auto continuousBoundaryAttributes =
        [](const BoundaryVertexRange& boundary) {
            for (std::size_t attribute = 0u;
                 attribute < boundary.attributeMinimum.size();
                 ++attribute) {
                if (boundary.attributeMaximum[attribute] -
                        boundary.attributeMinimum[attribute] >
                    0.001f) {
                    return false;
                }
            }
            return true;
        };
    if (!continuousBoundaryAttributes(westLawnBoundary) ||
        !continuousBoundaryAttributes(eastLawnBoundary)) {
        const auto spans = [](const BoundaryVertexRange& boundary) {
            std::string result;
            for (std::size_t attribute = 0u;
                 attribute < boundary.attributeMinimum.size();
                 ++attribute) {
                if (!result.empty()) {
                    result += ",";
                }
                result += std::to_string(
                    boundary.attributeMaximum[attribute] -
                    boundary.attributeMinimum[attribute]);
            }
            return result;
        };
        outFail =
            "Tint-normalized and ordinary light-lawn cells did not share continuous height/UV fields (west spans=" +
            spans(westLawnBoundary) + ", east spans=" +
            spans(eastLawnBoundary) + ").";
        return false;
    }
    if (loweredLawnVertexCount == 0u ||
        (maximumLawnUv2U - minimumLawnUv2U < 0.001f &&
         maximumLawnUv2V - minimumLawnUv2V < 0.001f)) {
        outFail =
            "Lowering a source light-lawn cell flattened its recovered leafy UV2 field to one plain sample.";
        return false;
    }
    std::sort(
        formerLedgeUv2Samples.begin(),
        formerLedgeUv2Samples.end(),
        [](const auto& left, const auto& right) {
            return left[0] < right[0];
        });
    if (formerLedgeUv2Samples.size() < 9u) {
        outFail =
            "The former x=22 ledge boundary did not expose its rebuilt five-centimetre UV lattice.";
        return false;
    }
    for (std::size_t index = 1u;
         index < formerLedgeUv2Samples.size();
         ++index) {
        const auto& previous = formerLedgeUv2Samples[index - 1u];
        const auto& current = formerLedgeUv2Samples[index];
        if (current[0] - previous[0] > 5.01f) {
            continue;
        }
        const float wrappedDeltaU = std::abs(
            (current[1] - previous[1]) -
            std::round(current[1] - previous[1]));
        const float wrappedDeltaV = std::abs(
            (current[2] - previous[2]) -
            std::round(current[2] - previous[2]));
        if (wrappedDeltaU > 0.02f || wrappedDeltaV > 0.02f) {
            outFail =
                "Rebuilding the lowered x=22 ledge retained its old raised-lawn mask boundary.";
            return false;
        }
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
