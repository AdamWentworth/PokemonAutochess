#include "engine/core/IAssetStore.h"
#include "engine/core/Paths.h"
#include "engine/assets/phlosion/PhlosionEnvironmentPatch.h"
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
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <unordered_map>
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

class PatchTextStore final : public engine::IAssetStore {
public:
    bool readText(
        const std::string& virtualPath,
        std::string& outText,
        std::string* outError) const override {
        const auto found = texts.find(virtualPath);
        if (found == texts.end()) {
            if (outError) *outError = "missing patch text";
            return false;
        }
        outText = found->second;
        return true;
    }
    bool readBytes(
        const std::string& virtualPath,
        std::vector<std::uint8_t>& outBytes,
        std::string* outError) const override {
        const auto found = texts.find(virtualPath);
        if (found == texts.end()) {
            if (outError) *outError = "missing patch bytes";
            return false;
        }
        outBytes.assign(found->second.begin(), found->second.end());
        return true;
    }
    bool exists(const std::string& virtualPath) const override {
        return texts.contains(virtualPath);
    }
    std::unordered_map<std::string, std::string> texts;
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

    // Prove that a project store can compose a source-locked mesh patch over
    // the isolated cooked scene without reading loose canonical assets.
    using namespace engine::assets::phlosion;
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        beforePatchBatches;
    environment.appendIndexedBatches(0.0f, beforePatchBatches);
    const auto terrainIndexCount = [](const auto& batches) {
        std::size_t count = 0u;
        for (const auto& batch : batches) {
            if (batch.geometryCacheKey.find("route1:terrain-") ==
                    std::string::npos &&
                batch.geometryCacheKey.find(":mesh:29:") ==
                    std::string::npos &&
                batch.geometryCacheKey.find(":mesh:30:") ==
                    std::string::npos &&
                batch.geometryCacheKey.find(":mesh:31:") ==
                    std::string::npos &&
                batch.geometryCacheKey.find(":mesh:32:") ==
                    std::string::npos &&
                batch.geometryCacheKey.find(":mesh:33:") ==
                    std::string::npos &&
                batch.geometryCacheKey.find(":mesh:34:") ==
                    std::string::npos &&
                batch.geometryCacheKey.find(":mesh:35:") ==
                    std::string::npos &&
                batch.geometryCacheKey.find(":mesh:36:") ==
                    std::string::npos) {
                continue;
            }
            const std::size_t indexCount = batch.sharedIndices
                ? batch.sharedIndexCount
                : batch.indices.size();
            count += indexCount * batch.instances.size();
        }
        return count;
    };
    const auto beforePatchTerrainIndexCount =
        terrainIndexCount(beforePatchBatches);
    EnvironmentPatchVertex patchA{};
    EnvironmentPatchVertex patchB{};
    EnvironmentPatchVertex patchC{};
    patchB.position[0] = 100.0f;
    patchC.position[2] = 100.0f;
    patchA.normal = patchB.normal = patchC.normal = {0.0f, 1.0f, 0.0f};
    patchA.tangent = patchB.tangent = patchC.tangent =
        {1.0f, 0.0f, 0.0f, 1.0f};
    patchA.bitangent = patchB.bitangent = patchC.bitangent =
        {0.0f, 0.0f, 1.0f, 1.0f};
    EnvironmentPatchDocument patchDocument{
        .source = EnvironmentPatchSourceLock{
            .profileId = "lgpe_route1_road001_00",
            .modelSha256 =
                "941EBF95032362D08D516D7502F15865FB1BA47CA78DFA0334DC665D3BDC2A86",
            .geometrySha256 =
                "B2604501D1941FD8A643E596EF866F68D8E90D5455EF1BFAE4A8D973DAC4BC44",
            .coordinateSystem = "source_centimetres_xyz_y_up"},
        .meshes = {EnvironmentPatchMesh{
            .id = "proof/runtime-triangle",
            .displayName = "Runtime Patch Triangle",
            .vertices = {patchA, patchB, patchC},
            .materialGroups = {EnvironmentPatchMaterialGroup{
                .materialIndex = 19u,
                .indices = {0u, 1u, 2u}}}}},
        .terrainReplacement = EnvironmentPatchTerrainReplacement{
            .tileSizeCm = 100.0f,
            .cells = {{17, -19}}}};
    PatchTextStore patchStore;
    constexpr char kPatchPath[] =
        "tests/generated/runtime-proof.patch.json";
    patchStore.texts[kPatchPath] =
        serializeEnvironmentPatchDocument(patchDocument);
    auto authored = environment.authoredScene();
    const auto environmentFolder = std::find_if(
        authored.nodes.begin(),
        authored.nodes.end(),
        [](const auto& node) {
            return node.folder() && node.displayName == "Environment";
        });
    std::string environmentFolderId;
    if (environmentFolder == authored.nodes.end()) {
        environmentFolderId = "folder/environment";
        authored.nodes.push_back(AuthoredSceneNode{
            .id = environmentFolderId,
            .displayName = "Environment"});
    } else {
        environmentFolderId = environmentFolder->id;
    }
    authored.nodes.push_back(AuthoredSceneNode{
        .id = "mesh-patch/runtime-proof",
        .displayName = "Runtime Patch Proof",
        .parentId = environmentFolderId,
        .siblingOrder = 999u,
        .transform = AuthoredSceneTransform{},
        .meshPatch = MeshPatchBinding{.assetPath = kPatchPath}});
    if (!environment.applyAuthoredScene(
            authored, patchStore, &error)) {
        outFail = "Source-locked environment patch was rejected: " + error;
        return false;
    }
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        patchBatches;
    environment.appendIndexedBatches(0.0f, patchBatches);
    if (terrainIndexCount(patchBatches) >=
        beforePatchTerrainIndexCount) {
        outFail =
            "The mounted environment patch did not retire its declared terrain replacement cell.";
        return false;
    }
    const auto filteredTerrain = std::find_if(
        patchBatches.begin(),
        patchBatches.end(),
        [](const auto& batch) {
            return batch.geometryCacheKey.find(
                ":terrain-replacement-mask:") !=
                std::string::npos;
        });
    if (filteredTerrain == patchBatches.end() ||
        !filteredTerrain->sharedIndices ||
        filteredTerrain->sharedIndexCount == 0u ||
        !filteredTerrain->indices.empty()) {
        outFail =
            "Terrain replacement masking did not publish cached shared index storage.";
        return false;
    }
    const auto* cachedTerrainIndices =
        filteredTerrain->sharedIndices;
    const auto cachedTerrainIndexCount =
        filteredTerrain->sharedIndexCount;
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        repeatedPatchBatches;
    environment.appendIndexedBatches(0.0f, repeatedPatchBatches);
    const auto repeatedFilteredTerrain = std::find_if(
        repeatedPatchBatches.begin(),
        repeatedPatchBatches.end(),
        [&](const auto& batch) {
            return batch.geometryCacheKey ==
                filteredTerrain->geometryCacheKey;
        });
    if (repeatedFilteredTerrain == repeatedPatchBatches.end() ||
        repeatedFilteredTerrain->sharedIndices !=
            cachedTerrainIndices ||
        repeatedFilteredTerrain->sharedIndexCount !=
            cachedTerrainIndexCount) {
        outFail =
            "Repeated terrain replacement submission did not reuse its cached index selection.";
        return false;
    }
    const bool foundPatchBatch = std::any_of(
        patchBatches.begin(),
        patchBatches.end(),
        [](const auto& batch) {
            return batch.geometryCacheKey.find(
                ":patch:mesh-patch/runtime-proof:mesh:0:group:0") !=
                std::string::npos;
        });
    if (!foundPatchBatch) {
        outFail =
            "The accepted environment patch did not reach indexed rendering with an isolated cache identity.";
        return false;
    }
    if (std::none_of(
            environment.layoutObjects().begin(),
            environment.layoutObjects().end(),
            [](const auto& object) {
                return object.stableId == "mesh-patch/runtime-proof" &&
                    object.targetKind == "environment_mesh_patch";
            })) {
        outFail =
            "The rendered environment patch is missing from the editor hierarchy projection.";
        return false;
    }
    if (!environment.applyBoardLayout(environment.layout(), &error) ||
        std::none_of(
            environment.authoredScene().nodes.begin(),
            environment.authoredScene().nodes.end(),
            [](const auto& node) {
                return node.meshPatch.has_value();
            })) {
        outFail =
            "A normal layout edit did not preserve the authored mesh-patch node: " +
            error;
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
    const route1::TerrainTileState exactSourceTile = *sourceTile;
    route1::BoardLayoutTransform exactShadowLayout =
        environment.layout();
    exactShadowLayout.authoredTerrainTiles.push_back(
        route1::AuthoredTerrainTile{
            .stableId = route1::route1TerrainTileStableId(25, -14),
            .displayName = "Terrain Tile (25, -14)",
            .categoryPath = "Environment/Terrain/Tiles",
            .tileSetAssetId = "route1/terrain_tileset",
            .gridX = 25,
            .gridZ = -14,
            .elevationLevel = exactSourceTile.elevationLevel,
            .surface = exactSourceTile.surface,
            .shape = exactSourceTile.shape,
            .visualVariant = "auto",
            .receivesProjectedShadow = false,
            .normalizeSourceTint = false,
            .suppressOverlappingVegetation = false,
            .reason = "terrain_shadow_receiver_authoring"});
    if (!environment.applyBoardLayout(exactShadowLayout, &error)) {
        outFail =
            "A source-identical shadow policy override was rejected: " +
            error;
        return false;
    }
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        exactShadowBatches;
    environment.appendIndexedBatches(0.0f, exactShadowBatches);
    const bool foundExactSourceSurface = std::any_of(
        exactShadowBatches.begin(),
        exactShadowBatches.end(),
        [](const auto& batch) {
            return batch.geometryCacheKey.starts_with(
                "route1:terrain-exact-source-surface:shadowless:");
        });
    const bool rebuiltExactSourceSurface = std::any_of(
        exactShadowBatches.begin(),
        exactShadowBatches.end(),
        [](const auto& batch) {
            return batch.geometryCacheKey.find(
                       "route1:terrain-authored-surface:") !=
                std::string::npos;
        });
    if (!foundExactSourceSurface || rebuiltExactSourceSurface) {
        outFail =
            "A render-only terrain edit did not resubmit the exact decoded source surface without procedural top geometry.";
        return false;
    }
    if (!environment.applyBoardLayout(neutralPreview, &error)) {
        outFail =
            "The source-identical shadow policy fixture could not restore the neutral Route 1 layout: " +
            error;
        return false;
    }
    const std::int32_t normalizedSourceElevation =
        exactSourceTile.elevationLevel;
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
            .elevationLevel = exactSourceTile.elevationLevel,
            .surface = exactSourceTile.surface,
            .shape = exactSourceTile.shape,
            .visualVariant = exactSourceTile.visualVariant,
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
        authoredTileFromSource(
            26,
            -14,
            normalizedSourceElevation - 1,
            "light_lawn",
            "auto"));
    loweredLawnLayout.authoredTerrainTiles.push_back(
        authoredTileFromSource(17, -1, 0, "light_lawn", "auto"));
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
    for (std::int32_t gridX = 22; gridX <= 24; ++gridX) {
        for (std::int32_t gridZ = -4; gridZ <= -1; ++gridZ) {
            auto eastOrdinaryTile = authoredTileFromSource(
                gridX,
                gridZ,
                0,
                gridZ == -2 ? "dirt_path" : "light_lawn",
                gridZ == -2 ? "path_10" : "auto");
            eastOrdinaryTile.receivesProjectedShadow = false;
            loweredLawnLayout.authoredTerrainTiles.push_back(
                std::move(eastOrdinaryTile));
        }
    }
    std::erase_if(
        loweredLawnLayout.authoredTerrainTiles,
        [](const route1::AuthoredTerrainTile& tile) {
            return tile.gridX >= 16 && tile.gridX <= 21 &&
                tile.gridZ == -12;
        });
    for (std::int32_t gridX = 16; gridX <= 21; ++gridX) {
        auto ledgeBaseLawn = authoredTileFromSource(
            gridX, -12, 1, "light_lawn", "auto");
        ledgeBaseLawn.reason =
            "terrain_flat_light_lawn_ledge_base_regression";
        loweredLawnLayout.authoredTerrainTiles.push_back(
            std::move(ledgeBaseLawn));
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
    bool staleLedgeCleanupOverlay = false;
    for (const auto& batch : loweredLawnBatches) {
        bool broadCleanupMesh = false;
        for (std::uint32_t meshIndex = 16u;
             meshIndex <= 27u;
             ++meshIndex) {
            if (batch.geometryCacheKey.find(
                    ":mesh:" + std::to_string(meshIndex) + ":") !=
                std::string::npos) {
                broadCleanupMesh = true;
                break;
            }
        }
        if (!broadCleanupMesh) {
            continue;
        }
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const std::size_t vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        const auto* indices = batch.sharedIndices
            ? batch.sharedIndices
            : batch.indices.data();
        const std::size_t indexCount = batch.sharedIndices
            ? batch.sharedIndexCount
            : batch.indices.size();
        const auto inspectInstance = [&](const auto& matrix) {
            for (std::size_t index = 0u;
                 vertices && indices && index + 2u < indexCount;
                 index += 3u) {
                std::array<std::array<float, 3>, 3> points{};
                bool valid = true;
                for (std::size_t corner = 0u; corner < 3u; ++corner) {
                    const auto vertexIndex = indices[index + corner];
                    if (vertexIndex >= vertexCount) {
                        valid = false;
                        break;
                    }
                    const auto& vertex = vertices[vertexIndex];
                    const auto sourcePoint = transformPoint(
                        sourceFromWorld,
                        transformPoint(
                            matrix,
                            {vertex.x, vertex.y, vertex.z}));
                    points[corner] = {
                        static_cast<float>(sourcePoint[0]),
                        static_cast<float>(sourcePoint[1]),
                        static_cast<float>(sourcePoint[2])};
                }
                if (!valid) {
                    continue;
                }
                for (std::int32_t gridX = 16;
                     gridX <= 22;
                     ++gridX) {
                    if (game::runtime::route1_environment::
                            route1TerrainCleanupCarrierIntersectsCellFootprint(
                                points,
                                {gridX, -12})) {
                        staleLedgeCleanupOverlay = true;
                        return;
                    }
                }
            }
        };
        if (batch.instances.empty()) {
            inspectInstance(batch.modelMatrix);
        } else {
            for (const auto& instance : batch.instances) {
                inspectInstance(instance.modelMatrix);
                if (staleLedgeCleanupOverlay) {
                    break;
                }
            }
        }
        if (staleLedgeCleanupOverlay) {
            break;
        }
    }
    if (staleLedgeCleanupOverlay) {
        outFail =
            "Broad LGPE cleanup overlays must not survive across either side of the rebuilt light-lawn material field below the Route 1 ramp ledge; the old lawn/dirt boundary stretches grass vertically over the canonical wall.";
        return false;
    }
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
    bool foundSourceFaithfulCliffGeometry = false;
    bool foundContinuousFringeField = false;
    bool foundSourceFringeMaskPhase = false;
    bool foundRebuiltUntouchedFringe = false;
    bool foundConcaveCliffJoin = false;
    bool foundConcaveFringeJoin = false;
    bool foundConcaveCrownUnderlay = false;
    bool foundLawnCornerUnderlay = false;
    bool foundConvexLawnCornerUnderlay = false;
    bool foundConvexLawnCornerPocketRepair = false;
    bool foundConvexLawnCapUnderlay = false;
    bool foundSourceHandoffUnderlay = false;
    bool foundFringeCorner = false;
    bool foundCliffCorner = false;
    bool foundAdvancingFringeCornerField = false;
    bool foundAdvancingCliffCornerField = false;
    bool foundInsetOrganicCliff = false;
    bool foundInsetOrganicFringe = false;
    bool foundFringeCrownSourcePlane = false;
    bool foundLowerLawnLedgeOverlap = false;
    bool foundLowerLawnFootColorBlend = false;
    bool foundFlatLightLawnLedgeBase = false;
    bool foundNormalLightLawnOneRowFromLedge = false;
    bool flatLightLawnLedgeBaseRemainsPlanar = true;
    bool foundDirtLawnFootColorBlend = false;
    bool foundDirtFootCoreColor = false;
    bool foundLowerLawnTerminalEdgeFill = false;
    bool retainedSourceGroundShelfAtLedgeBase = false;
    bool ledgeBaseGroundOverlayIndicesValid = true;
    bool foundUpperLawnCrownClip = false;
    bool foundUpperLawnCrownNormal = false;
    bool foundUpperSourceLawnFields = false;
    bool foundNormalizedUpperLawnCrownClip = false;
    bool foundNormalizedUpperLawnCrownNormal = false;
    bool foundNormalizedUpperLightLawnFields = false;
    float maximumUpperLawnCrownX =
        std::numeric_limits<float>::lowest();
    float normalizedMidCrownX =
        std::numeric_limits<float>::lowest();
    std::array<float, 3> normalizedMidCrownNormal{};
    std::vector<float> upperLawnCrossSectionX;
    float maximumLowerLawnTerminalContactZ =
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
                "mesh:0:group:0:terrain-mask:") !=
            std::string::npos) {
            const auto* overlayVertices = batch.sharedVertices
                ? batch.sharedVertices
                : batch.vertices.data();
            const std::size_t overlayVertexCount = batch.sharedVertices
                ? batch.sharedVertexCount
                : batch.vertices.size();
            const auto* overlayIndices = batch.sharedIndices
                ? batch.sharedIndices
                : batch.indices.data();
            const std::size_t overlayIndexCount = batch.sharedIndices
                ? batch.sharedIndexCount
                : batch.indices.size();
            for (std::size_t index = 0u;
                 overlayVertices && overlayIndices &&
                 index < overlayIndexCount;
                 ++index) {
                const auto vertexIndex = overlayIndices[index];
                if (vertexIndex >= overlayVertexCount) {
                    ledgeBaseGroundOverlayIndicesValid = false;
                    continue;
                }
                const auto& vertex = overlayVertices[vertexIndex];
                retainedSourceGroundShelfAtLedgeBase =
                    retainedSourceGroundShelfAtLedgeBase ||
                    (vertex.x >= 1600.0f && vertex.x <= 2200.0f &&
                     vertex.z > -1199.9f && vertex.z < -1100.1f &&
                     vertex.y >= 45.0f && vertex.y <= 55.0f);
            }
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-cliff-concave-corner:") !=
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
            float minimumU = std::numeric_limits<float>::max();
            float maximumU = std::numeric_limits<float>::lowest();
            bool insideSourceCornerEnvelope = true;
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                minimumU = std::min(
                    minimumU, vertices[vertexIndex].sourceUv1U);
                maximumU = std::max(
                    maximumU, vertices[vertexIndex].sourceUv1U);
                insideSourceCornerEnvelope =
                    insideSourceCornerEnvelope &&
                    std::abs(vertices[vertexIndex].x) <= 25.5f &&
                    std::abs(vertices[vertexIndex].z) <= 25.5f;
            }
            if (batch.geometryCacheKey.find(":levels-1") !=
                    std::string::npos &&
                (vertexCount != 54u || indexCount != 144u ||
                 !insideSourceCornerEnvelope ||
                 maximumU - minimumU <= 0.20f)) {
                outFail =
                    "A rebuilt one-level Route 1 concave cliff did not preserve the compact decoded source handoff, duplicated material bands, or advancing contour field: " +
                    batch.geometryCacheKey;
                return false;
            }
            foundConcaveCliffJoin = true;
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-fringe-concave-corner:") !=
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
            float minimumU = std::numeric_limits<float>::max();
            float maximumU = std::numeric_limits<float>::lowest();
            bool insideSourceCornerEnvelope = true;
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                minimumU = std::min(
                    minimumU, vertices[vertexIndex].sourceUv1U);
                maximumU = std::max(
                    maximumU, vertices[vertexIndex].sourceUv1U);
                insideSourceCornerEnvelope =
                    insideSourceCornerEnvelope &&
                    std::abs(vertices[vertexIndex].x) <= 25.5f &&
                    std::abs(vertices[vertexIndex].z) <= 25.5f;
            }
            if (vertexCount != 27u || indexCount != 96u ||
                !insideSourceCornerEnvelope ||
                maximumU - minimumU <= 0.20f) {
                outFail =
                    "A rebuilt Route 1 concave fringe did not preserve the complete three-row decoded source handoff and advancing mask field: " +
                    batch.geometryCacheKey;
                return false;
            }
            foundConcaveFringeJoin = true;
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-concave-crown:") !=
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
            bool crownIsAnUnderlay = true;
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                crownIsAnUnderlay = crownIsAnUnderlay &&
                    vertices[vertexIndex].y < 0.0f;
            }
            if (vertexCount != 18u || indexCount != 48u ||
                !crownIsAnUnderlay) {
                outFail =
                    "A rebuilt Route 1 concave corner did not submit its two-row lawn-to-fringe carrier strictly beneath the ordinary lawn: " +
                    batch.geometryCacheKey;
                return false;
            }
            foundConcaveCrownUnderlay = true;
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-lawn-corner-underlay:") !=
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
            float maximumRadiusCm = 0.0f;
            bool underlayIsBelowLawn = true;
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                maximumRadiusCm = std::max(
                    maximumRadiusCm,
                    std::hypot(
                        vertices[vertexIndex].x,
                        vertices[vertexIndex].z));
                underlayIsBelowLawn = underlayIsBelowLawn &&
                    vertices[vertexIndex].y < 0.0f;
            }
            if (vertexCount != 25u || indexCount != 72u ||
                !underlayIsBelowLawn ||
                maximumRadiusCm < 19.9f ||
                maximumRadiusCm > 20.1f) {
                outFail =
                    "A rebuilt Route 1 ledge corner did not submit its narrow hidden lawn seam carrier: " +
                    batch.geometryCacheKey;
                return false;
            }
            foundLawnCornerUnderlay = true;
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-convex-lawn-corner-underlay:") !=
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
            const auto* indices = batch.sharedIndices
                ? batch.sharedIndices
                : batch.indices.data();
            bool repairIsAboveLawn = true;
            float maximumRadiusCm = 0.0f;
            float maximumTriangleEdgeCm = 0.0f;
            bool usesOpaqueCarrierField = true;
            const bool lightLawnCarrier =
                batch.geometryCacheKey.find(
                    ":surface-light_lawn:") != std::string::npos;
            const bool darkLawnCarrier =
                batch.geometryCacheKey.find(
                    ":surface-dark_lawn:") != std::string::npos;
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                repairIsAboveLawn = repairIsAboveLawn &&
                    vertices[vertexIndex].y > 0.02f &&
                    vertices[vertexIndex].y < 0.04f;
                maximumRadiusCm = std::max(
                    maximumRadiusCm,
                    std::hypot(
                        vertices[vertexIndex].x,
                        vertices[vertexIndex].z));
                if (lightLawnCarrier) {
                    usesOpaqueCarrierField =
                        usesOpaqueCarrierField &&
                        std::abs(
                            vertices[vertexIndex].sourceUv2U +
                                0.101646f) <= 0.001f &&
                        std::abs(
                            vertices[vertexIndex].sourceUv2V +
                                1.071291f) <= 0.001f;
                } else if (darkLawnCarrier) {
                    usesOpaqueCarrierField =
                        usesOpaqueCarrierField &&
                        std::abs(
                            vertices[vertexIndex].sourceUv2U +
                                0.049999952f) <= 0.001f &&
                        std::abs(
                            vertices[vertexIndex].sourceUv2V -
                                0.949999988f) <= 0.001f;
                }
            }
            for (std::size_t index = 0u;
                 index + 2u < indexCount;
                 index += 3u) {
                for (std::size_t corner = 0u; corner < 3u; ++corner) {
                    const std::size_t next = (corner + 1u) % 3u;
                    if (indices[index + corner] >= vertexCount ||
                        indices[index + next] >= vertexCount) {
                        maximumTriangleEdgeCm =
                            std::numeric_limits<float>::infinity();
                        continue;
                    }
                    const auto& first = vertices[indices[index + corner]];
                    const auto& second = vertices[indices[index + next]];
                    maximumTriangleEdgeCm = std::max(
                        maximumTriangleEdgeCm,
                        std::hypot(
                            first.x - second.x,
                            first.z - second.z));
                }
            }
            if (vertexCount < 50u || indexCount < 150u ||
                !repairIsAboveLawn ||
                maximumRadiusCm < 51.9f ||
                maximumRadiusCm > 52.1f ||
                maximumTriangleEdgeCm > 7.2f ||
                !usesOpaqueCarrierField) {
                outFail =
                    "A rebuilt Route 1 convex ledge corner did not split its low contact repair into a donor-owned authoritative opaque quarter patch: " +
                    batch.geometryCacheKey;
                return false;
            }
            foundConvexLawnCornerUnderlay = true;
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-convex-lawn-corner-pocket-repair:") !=
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
            const auto* indices = batch.sharedIndices
                ? batch.sharedIndices
                : batch.indices.data();
            bool repairIsAboveLawn = true;
            float maximumRadiusCm = 0.0f;
            float maximumTriangleEdgeCm = 0.0f;
            bool usesOpaqueCarrierField = true;
            const bool lightLawnCarrier =
                batch.geometryCacheKey.find(
                    ":surface-light_lawn:") != std::string::npos;
            const bool darkLawnCarrier =
                batch.geometryCacheKey.find(
                    ":surface-dark_lawn:") != std::string::npos;
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                repairIsAboveLawn = repairIsAboveLawn &&
                    vertices[vertexIndex].y > 0.02f &&
                    vertices[vertexIndex].y < 0.04f;
                maximumRadiusCm = std::max(
                    maximumRadiusCm,
                    std::hypot(
                        vertices[vertexIndex].x,
                        vertices[vertexIndex].z));
                if (lightLawnCarrier) {
                    usesOpaqueCarrierField =
                        usesOpaqueCarrierField &&
                        std::abs(
                            vertices[vertexIndex].sourceUv2U +
                                0.101646f) <= 0.001f &&
                        std::abs(
                            vertices[vertexIndex].sourceUv2V +
                                1.071291f) <= 0.001f;
                } else if (darkLawnCarrier) {
                    usesOpaqueCarrierField =
                        usesOpaqueCarrierField &&
                        std::abs(
                            vertices[vertexIndex].sourceUv2U +
                                0.049999952f) <= 0.001f &&
                        std::abs(
                            vertices[vertexIndex].sourceUv2V -
                                0.949999988f) <= 0.001f;
                }
            }
            for (std::size_t index = 0u;
                 index + 2u < indexCount;
                 index += 3u) {
                for (std::size_t corner = 0u; corner < 3u; ++corner) {
                    const std::size_t next = (corner + 1u) % 3u;
                    if (indices[index + corner] >= vertexCount ||
                        indices[index + next] >= vertexCount) {
                        maximumTriangleEdgeCm =
                            std::numeric_limits<float>::infinity();
                        continue;
                    }
                    const auto& first = vertices[indices[index + corner]];
                    const auto& second = vertices[indices[index + next]];
                    maximumTriangleEdgeCm = std::max(
                        maximumTriangleEdgeCm,
                        std::hypot(
                            first.x - second.x,
                            first.z - second.z));
                }
            }
            if (vertexCount < 20u || indexCount < 45u ||
                !repairIsAboveLawn ||
                maximumRadiusCm < 33.9f ||
                maximumRadiusCm > 34.1f ||
                maximumTriangleEdgeCm > 7.2f ||
                !usesOpaqueCarrierField) {
                outFail =
                    "A rebuilt Route 1 convex ledge corner did not assign its recessed low-elevation pocket to two authoritative donor halves: " +
                    batch.geometryCacheKey +
                    " (vertices=" + std::to_string(vertexCount) +
                    ", indices=" + std::to_string(indexCount) +
                    ", above=" + std::to_string(repairIsAboveLawn) +
                    ", radius=" + std::to_string(maximumRadiusCm) +
                    ", max-edge=" +
                    std::to_string(maximumTriangleEdgeCm) +
                    ", opaque=" +
                    std::to_string(usesOpaqueCarrierField) + ")";
                return false;
            }
            foundConvexLawnCornerPocketRepair = true;
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-convex-lawn-cap-underlay:") !=
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
            bool usesRaisedCrownField = true;
            float maximumAbsoluteHeightCm = 0.0f;
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                maximumAbsoluteHeightCm = std::max(
                    maximumAbsoluteHeightCm,
                    std::abs(vertices[vertexIndex].y));
                usesRaisedCrownField =
                    usesRaisedCrownField &&
                    std::abs(
                        vertices[vertexIndex].sourceUv1U +
                            0.049999952f) <= 0.001f &&
                    std::abs(
                        vertices[vertexIndex].sourceUv1V -
                            0.949999988f) <= 0.001f &&
                    std::abs(
                        vertices[vertexIndex].sourceUv2U +
                            0.049999952f) <= 0.001f &&
                    std::abs(
                        vertices[vertexIndex].sourceUv2V -
                            0.949999988f) <= 0.001f &&
                    std::abs(vertices[vertexIndex].r -
                        0.180392161f) <= 0.001f &&
                    std::abs(vertices[vertexIndex].g -
                        0.482352942f) <= 0.001f &&
                    std::abs(vertices[vertexIndex].b -
                        0.431372553f) <= 0.001f;
            }
            float maximumTriangleEdgeCm = 0.0f;
            bool hasInvalidTriangle = indexCount % 3u != 0u;
            for (std::size_t index = 0u;
                 index + 2u < indexCount;
                 index += 3u) {
                const auto& first =
                    vertices[batch.sharedIndices
                        ? batch.sharedIndices[index]
                        : batch.indices[index]];
                const auto& second =
                    vertices[batch.sharedIndices
                        ? batch.sharedIndices[index + 1u]
                        : batch.indices[index + 1u]];
                const auto& third =
                    vertices[batch.sharedIndices
                        ? batch.sharedIndices[index + 2u]
                        : batch.indices[index + 2u]];
                const float signedAreaTwice =
                    (second.x - first.x) *
                        (third.z - first.z) -
                    (second.z - first.z) *
                        (third.x - first.x);
                hasInvalidTriangle =
                    hasInvalidTriangle ||
                    signedAreaTwice <= 0.0001f;
                const auto edgeLength =
                    [](const auto& left, const auto& right) {
                        return std::hypot(
                            left.x - right.x,
                            left.z - right.z);
                    };
                maximumTriangleEdgeCm = std::max({
                    maximumTriangleEdgeCm,
                    edgeLength(first, second),
                    edgeLength(second, third),
                    edgeLength(third, first)});
            }
            if (vertexCount < 50u || indexCount < 250u ||
                maximumAbsoluteHeightCm > 10.0f ||
                maximumTriangleEdgeCm > 7.2f ||
                hasInvalidTriangle ||
                !usesRaisedCrownField) {
                outFail =
                    "A rebuilt Route 1 convex ledge cap did not submit its locally tessellated contour carrier: " +
                    batch.geometryCacheKey +
                    " (vertices=" + std::to_string(vertexCount) +
                    ", indices=" + std::to_string(indexCount) +
                    ", max-height-cm=" +
                    std::to_string(maximumAbsoluteHeightCm) +
                    ", max-edge-cm=" +
                    std::to_string(maximumTriangleEdgeCm) +
                    ", invalid-triangle=" +
                    std::to_string(hasInvalidTriangle) +
                    ", raised-crown-field=" +
                    std::to_string(usesRaisedCrownField) + ")";
                return false;
            }
            foundConvexLawnCapUnderlay = true;
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-source-handoff-underlay:") !=
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
            bool carrierIsAboveSourceAndRebuiltPlanes = true;
            bool usesOpaqueCarrierField = true;
            const bool raisedLawnCarrier =
                batch.geometryCacheKey.find(":level-0:") ==
                std::string::npos;
            const bool lightLawnCarrier =
                batch.geometryCacheKey.find(
                    ":surface-light_lawn:") != std::string::npos;
            const bool darkLawnCarrier =
                batch.geometryCacheKey.find(
                    ":surface-dark_lawn:") != std::string::npos;
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                carrierIsAboveSourceAndRebuiltPlanes =
                    carrierIsAboveSourceAndRebuiltPlanes &&
                    vertices[vertexIndex].y > 0.02f &&
                    vertices[vertexIndex].y < 0.04f;
                if (raisedLawnCarrier || darkLawnCarrier) {
                    usesOpaqueCarrierField =
                        usesOpaqueCarrierField &&
                        std::abs(
                            vertices[vertexIndex].sourceUv1U +
                                0.049999952f) <= 0.001f &&
                        std::abs(
                            vertices[vertexIndex].sourceUv1V -
                                0.949999988f) <= 0.001f &&
                        std::abs(
                            vertices[vertexIndex].sourceUv2U +
                                0.049999952f) <= 0.001f &&
                        std::abs(
                            vertices[vertexIndex].sourceUv2V -
                                0.949999988f) <= 0.001f;
                } else if (lightLawnCarrier) {
                    usesOpaqueCarrierField =
                        usesOpaqueCarrierField &&
                        std::abs(
                            vertices[vertexIndex].sourceUv2U +
                                0.101646f) <= 0.001f &&
                        std::abs(
                            vertices[vertexIndex].sourceUv2V +
                                1.071291f) <= 0.001f;
                }
            }
            if (vertexCount != 43u || indexCount != 126u ||
                !carrierIsAboveSourceAndRebuiltPlanes ||
                !usesOpaqueCarrierField) {
                outFail =
                    "A rebuilt Route 1 generated/source lawn handoff did not submit its selector-coherent authoritative seam strip: " +
                    batch.geometryCacheKey;
                return false;
            }
            foundSourceHandoffUnderlay = true;
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-cliff-corner:") !=
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
            float minimumCornerU =
                std::numeric_limits<float>::max();
            float maximumCornerU =
                std::numeric_limits<float>::lowest();
            if (batch.geometryCacheKey.find(":levels-1") !=
                    std::string::npos &&
                (vertexCount != 54u || indexCount != 144u)) {
                outFail =
                    "A rebuilt one-level Route 1 cliff corner did not duplicate its three source material bands: " +
                    batch.geometryCacheKey;
                return false;
            }
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                minimumCornerU = std::min(
                    minimumCornerU,
                    vertices[vertexIndex].sourceUv1U);
                maximumCornerU = std::max(
                    maximumCornerU,
                    vertices[vertexIndex].sourceUv1U);
                if (std::abs(vertices[vertexIndex].x) > 50.001f ||
                    std::abs(vertices[vertexIndex].z) > 50.001f) {
                    outFail =
                        "A rebuilt Route 1 cliff corner exceeded its owning tile footprint: " +
                        batch.geometryCacheKey;
                    return false;
                }
            }
            foundAdvancingCliffCornerField =
                foundAdvancingCliffCornerField ||
                maximumCornerU - minimumCornerU > 0.25f;
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
            float minimumCornerU =
                std::numeric_limits<float>::max();
            float maximumCornerU =
                std::numeric_limits<float>::lowest();
            if (vertexCount != 27u || indexCount != 96u) {
                outFail =
                    "A rebuilt convex Route 1 corner did not submit the complete three-row leafy quarter-arc: " +
                    batch.geometryCacheKey;
                return false;
            }
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                minimumCornerU = std::min(
                    minimumCornerU,
                    vertices[vertexIndex].sourceUv1U);
                maximumCornerU = std::max(
                    maximumCornerU,
                    vertices[vertexIndex].sourceUv1U);
                if (std::abs(vertices[vertexIndex].x) > 50.001f ||
                    std::abs(vertices[vertexIndex].z) > 50.001f) {
                    outFail =
                        "A rebuilt Route 1 leafy corner exceeded its owning tile footprint: " +
                        batch.geometryCacheKey;
                    return false;
                }
            }
            foundAdvancingFringeCornerField =
                foundAdvancingFringeCornerField ||
                maximumCornerU - minimumCornerU > 0.24f;
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
                    (vertex.sourceUv1V <= 0.0026f &&
                     std::abs(vertex.r - 0.180392161f) <= 0.001f &&
                     std::abs(vertex.g - 0.482352942f) <= 0.001f &&
                     std::abs(vertex.b - 0.431372553f) <= 0.001f);
                foundUpperWhite = foundUpperWhite ||
                    (vertex.sourceUv1V >= 0.699f &&
                     std::abs(vertex.r - 1.0f) <= 0.001f &&
                     std::abs(vertex.g - 1.0f) <= 0.001f &&
                     std::abs(vertex.b - 1.0f) <= 0.001f);
                foundAdvancingLowerMask =
                    foundAdvancingLowerMask ||
                    (vertex.sourceUv1V <= 0.3384f &&
                     std::abs(vertex.sourceUv2U + 0.05f) > 0.001f);
                foundNeutralUpperMask =
                    foundNeutralUpperMask ||
                    (vertex.sourceUv1V >= 0.699f &&
                     std::abs(vertex.sourceUv2U + 0.05f) <= 0.001f &&
                     std::abs(vertex.sourceUv2V - 0.85f) <= 0.001f);
                if (vertex.sourceUv1V <= 0.0026f) {
                    minimumFootOutward = std::min(
                        minimumFootOutward, vertex.z);
                    maximumFootOutward = std::max(
                        maximumFootOutward, vertex.z);
                }
                if (vertex.sourceUv1V >= 0.9974f) {
                    minimumCrownOutward = std::min(
                        minimumCrownOutward, vertex.z);
                    maximumCrownOutward = std::max(
                        maximumCrownOutward, vertex.z);
                }
            }
            const bool sourceGeometryHeight =
                std::abs(minimumY) <= 0.001f &&
                maximumY >= 47.999f &&
                maximumY <= 50.001f;
            const bool generatedGeometryHeight =
                std::abs(minimumY - -0.02f) <= 0.001f &&
                std::abs(maximumY - (48.0f - 0.02f)) <= 0.001f;
            if (vertexCount != 126u || indexCount != 360u ||
                (!sourceGeometryHeight && !generatedGeometryHeight) ||
                !foundLowerTint || !foundUpperWhite ||
                !foundAdvancingLowerMask ||
                !foundNeutralUpperMask) {
                outFail =
                    "A rebuilt one-level Route 1 cliff no longer preserves the source mesh's duplicated bands, profile, lower tint, or UV2 mask transition (vertices=" +
                    std::to_string(vertexCount) +
                    ", indices=" + std::to_string(indexCount) +
                    ", y=" + std::to_string(minimumY) + ".." +
                    std::to_string(maximumY) +
                    ", source-height=" +
                    std::to_string(sourceGeometryHeight) +
                    ", generated-height=" +
                    std::to_string(generatedGeometryHeight) +
                    ", lower-tint=" + std::to_string(foundLowerTint) +
                    ", upper-white=" + std::to_string(foundUpperWhite) +
                    ", lower-mask=" +
                    std::to_string(foundAdvancingLowerMask) +
                    ", upper-mask=" +
                    std::to_string(foundNeutralUpperMask) + "): " +
                    batch.geometryCacheKey;
                return false;
            }
            const bool validSourceOutwardProfile =
                sourceGeometryHeight &&
                minimumFootOutward >= -50.001f &&
                maximumFootOutward <= 15.001f &&
                minimumCrownOutward >= -50.001f &&
                maximumCrownOutward <= -9.999f;
            const bool validGeneratedOutwardProfile =
                !sourceGeometryHeight &&
                maximumFootOutward <= 3.0f &&
                maximumCrownOutward <= -22.0f &&
                maximumFootOutward - minimumFootOutward >= 1.0f &&
                maximumCrownOutward - minimumCrownOutward >= 1.0f;
            if (!validSourceOutwardProfile &&
                !validGeneratedOutwardProfile) {
                outFail =
                    "A rebuilt Route 1 cliff lost its measured inward foot/crown offsets or regressed to a ruler-straight strip (foot=" +
                    std::to_string(minimumFootOutward) + ".." +
                    std::to_string(maximumFootOutward) +
                    ", crown=" +
                    std::to_string(minimumCrownOutward) + ".." +
                    std::to_string(maximumCrownOutward) +
                    ", source=" +
                    std::to_string(sourceGeometryHeight) + "): " +
                    batch.geometryCacheKey;
                return false;
            }
            foundInsetOrganicCliff = true;
            foundSourceFaithfulCliffBands = true;
            foundSourceFaithfulCliffGeometry =
                foundSourceFaithfulCliffGeometry ||
                sourceGeometryHeight;
            if (batch.geometryCacheKey.find(
                    ":tile-levels-1-1:neighbor-levels-0-0:") !=
                std::string::npos) {
                minimumFullCliffCrownOutward = std::min(
                    minimumFullCliffCrownOutward,
                    minimumCrownOutward);
                maximumFullCliffCrownOutward = std::max(
                    maximumFullCliffCrownOutward,
                    maximumCrownOutward);
            }
        }
        if (batch.geometryCacheKey.find(
                "route1:terrain-fringe:") != std::string::npos) {
            const auto* vertices = batch.sharedVertices
                ? batch.sharedVertices
                : batch.vertices.data();
            const std::size_t vertexCount = batch.sharedVertices
                ? batch.sharedVertexCount
                : batch.vertices.size();
            if (vertexCount == 63u &&
                (batch.sharedIndices
                    ? batch.sharedIndexCount
                    : batch.indices.size()) == 240u) {
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
                        foundFringeCrownSourcePlane =
                            foundFringeCrownSourcePlane ||
                            std::abs(vertex.y) <= 0.001f;
                    }
                }
                foundContinuousFringeField =
                    foundContinuousFringeField ||
                    (std::abs(
                         maximumMaskU - minimumMaskU -
                         0.495f) <= 0.001f &&
                     foundGreenCrown && foundSlopedCarrier);
                if (batch.geometryCacheKey.find(
                        "route1:terrain-fringe:cell-16--4:edge-2:") !=
                    std::string::npos) {
                    foundRebuiltUntouchedFringe = true;
                }
                if (batch.geometryCacheKey.find(
                        ":material-contour-cm-0:") !=
                    std::string::npos) {
                    foundSourceFringeMaskPhase =
                        foundSourceFringeMaskPhase ||
                        std::abs(minimumMaskU - 0.2841f) <= 0.001f;
                }
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
                (vertex.x >= 1696.49f && vertex.x <= 1696.51f &&
                 vertex.z >= -100.01f && vertex.z <= 0.01f &&
                 std::abs(vertex.y - 0.02f) <= 0.01f);
            foundLowerLawnFootColorBlend =
                foundLowerLawnFootColorBlend ||
                (vertex.x >= 1696.49f && vertex.x <= 1696.51f &&
                 vertex.z >= -100.01f && vertex.z <= 0.01f &&
                 std::abs(vertex.y - 0.02f) <= 0.01f &&
                 std::abs(vertex.r - 0.180392161f) <= 0.001f &&
                 std::abs(vertex.g - 0.482352942f) <= 0.001f &&
                 std::abs(vertex.b - 0.431372553f) <= 0.001f);
            if (vertex.x >= 1600.0f && vertex.x <= 2200.0f &&
                vertex.z >= -1200.01f && vertex.z <= -1099.99f) {
                foundFlatLightLawnLedgeBase = true;
                flatLightLawnLedgeBaseRemainsPlanar =
                    flatLightLawnLedgeBaseRemainsPlanar &&
                    std::abs(vertex.y - 50.02f) <= 0.01f;
                if (vertex.z >= -1195.01f &&
                    vertex.z <= -1194.99f &&
                    vertex.r >= 0.60f &&
                    vertex.g >= 0.72f &&
                    vertex.b >= 0.60f) {
                    foundNormalLightLawnOneRowFromLedge = true;
                }
            }
            foundDirtLawnFootColorBlend =
                foundDirtLawnFootColorBlend ||
                (vertex.x >= 2503.49f && vertex.x <= 2503.51f &&
                 std::abs(vertex.z + 100.0f) <= 0.01f &&
                 std::abs(vertex.y - 0.02f) <= 0.01f &&
                 std::abs(vertex.r - 0.180392161f) <= 0.001f &&
                 std::abs(vertex.g - 0.482352942f) <= 0.001f &&
                 std::abs(vertex.b - 0.431372553f) <= 0.001f);
            foundDirtFootCoreColor =
                foundDirtFootCoreColor ||
                (vertex.x >= 2500.0f && vertex.x <= 2504.0f &&
                 std::abs(vertex.z + 150.0f) <= 0.01f &&
                 std::abs(vertex.y - 0.02f) <= 0.01f &&
                 std::abs(vertex.r - 0.905882359f) <= 0.001f &&
                 std::abs(vertex.g - 0.815686285f) <= 0.001f &&
                 std::abs(vertex.b - 0.631372571f) <= 0.001f);
            foundLowerLawnTerminalEdgeFill =
                foundLowerLawnTerminalEdgeFill ||
                (vertex.x >= 1696.49f && vertex.x <= 1696.51f &&
                 std::abs(vertex.z) <= 0.01f &&
                 std::abs(vertex.y - 0.02f) <= 0.01f);
            foundUpperLawnCrownClip =
                foundUpperLawnCrownClip ||
                (vertex.x >= 1674.0f && vertex.x <= 1676.2f &&
                 vertex.z >= -100.01f && vertex.z <= 0.01f &&
                 std::abs(vertex.y - 50.02f) <= 0.01f);
            foundUpperLawnCrownNormal =
                foundUpperLawnCrownNormal ||
                (vertex.x >= 1674.0f && vertex.x <= 1676.2f &&
                 std::abs(vertex.z + 50.0f) <= 0.01f &&
                 std::abs(vertex.y - 50.02f) <= 0.01f &&
                 std::abs(vertex.nx - 0.053405762f) <= 0.002f &&
                 std::abs(vertex.ny - 0.998535156f) <= 0.002f &&
                 std::abs(vertex.nz) <= 0.002f);
            const bool usesDarkLawnSourceFields =
                std::abs(vertex.sourceUv2U + 0.05f) <= 0.001f &&
                std::abs(vertex.sourceUv2V - 0.95f) <= 0.001f &&
                std::abs(vertex.r - 0.180392161f) <= 0.001f &&
                std::abs(vertex.g - 0.482352942f) <= 0.001f &&
                std::abs(vertex.b - 0.431372553f) <= 0.001f;
            foundUpperSourceLawnFields =
                foundUpperSourceLawnFields ||
                (vertex.x >= 1674.0f && vertex.x <= 1676.2f &&
                 vertex.z >= -100.01f && vertex.z <= 0.01f &&
                 std::abs(vertex.y - 50.02f) <= 0.01f &&
                 usesDarkLawnSourceFields);
            foundNormalizedUpperLightLawnFields =
                foundNormalizedUpperLightLawnFields ||
                (vertex.x >= 2500.0f && vertex.x <= 2600.0f &&
                 vertex.z >= -1400.01f && vertex.z <= -1299.99f &&
                 std::abs(
                     vertex.y -
                     (static_cast<float>(normalizedSourceElevation) *
                          50.0f +
                      0.02f)) <= 0.01f &&
                 !usesDarkLawnSourceFields);
            foundNormalizedUpperLawnCrownClip =
                foundNormalizedUpperLawnCrownClip ||
                (vertex.x >= 2576.0f && vertex.x <= 2578.2f &&
                 vertex.z >= -1400.01f && vertex.z <= -1299.99f &&
                 std::abs(
                     vertex.y -
                     (static_cast<float>(normalizedSourceElevation) *
                          50.0f +
                      0.02f)) <= 0.01f);
            foundNormalizedUpperLawnCrownNormal =
                foundNormalizedUpperLawnCrownNormal ||
                (vertex.x >= 2576.0f && vertex.x <= 2578.2f &&
                 std::abs(vertex.z + 1350.0f) <= 0.01f &&
                 std::abs(
                     vertex.y -
                     (static_cast<float>(normalizedSourceElevation) *
                          50.0f +
                      0.02f)) <= 0.01f &&
                 std::abs(vertex.nx - 0.053405762f) <= 0.002f &&
                 std::abs(vertex.ny - 0.998535156f) <= 0.002f &&
                 std::abs(vertex.nz) <= 0.002f);
            if (vertex.x >= 2500.0f && vertex.x <= 2600.0f &&
                std::abs(vertex.z + 1350.0f) <= 0.01f &&
                std::abs(
                    vertex.y -
                    (static_cast<float>(normalizedSourceElevation) *
                         50.0f +
                     0.02f)) <= 0.01f &&
                vertex.x > normalizedMidCrownX) {
                normalizedMidCrownX = vertex.x;
                normalizedMidCrownNormal = {
                    vertex.nx, vertex.ny, vertex.nz};
            }
            if (vertex.x >= 1600.0f && vertex.x <= 1700.0f &&
                vertex.z >= -100.01f && vertex.z <= 0.01f &&
                std::abs(vertex.y - 50.02f) <= 0.01f) {
                maximumUpperLawnCrownX = std::max(
                    maximumUpperLawnCrownX, vertex.x);
                if (std::abs(vertex.z + 50.0f) <= 0.01f) {
                    upperLawnCrossSectionX.push_back(vertex.x);
                }
            }
            if (vertex.x >= 1696.49f && vertex.x <= 1696.51f &&
                vertex.z >= -100.01f && vertex.z <= 0.01f &&
                std::abs(vertex.y - 0.02f) <= 0.01f) {
                maximumLowerLawnTerminalContactZ = std::max(
                    maximumLowerLawnTerminalContactZ, vertex.z);
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
        !foundSourceFaithfulCliffGeometry ||
        !foundContinuousFringeField ||
        !foundSourceFringeMaskPhase ||
        foundRebuiltUntouchedFringe ||
        !foundConcaveCliffJoin ||
        !foundConcaveFringeJoin ||
        !foundConcaveCrownUnderlay ||
        !foundLawnCornerUnderlay ||
        !foundConvexLawnCornerUnderlay ||
        !foundConvexLawnCornerPocketRepair ||
        !foundConvexLawnCapUnderlay ||
        !foundSourceHandoffUnderlay ||
        !foundFringeCorner ||
        !foundCliffCorner ||
        !foundAdvancingFringeCornerField ||
        !foundAdvancingCliffCornerField ||
        !foundInsetOrganicCliff ||
        !foundInsetOrganicFringe ||
        !foundFringeCrownSourcePlane) {
        outFail =
            "Lowering Route 1 terrain did not submit the measured inset organic cliff/fringe profiles, overlap their crown with the rebuilt top, preserve concave joins, or keep both rounded corner carriers inside their owning tile (cliff-bands=" +
            std::to_string(foundSourceFaithfulCliffBands) +
            ", source-cliff-geometry=" +
            std::to_string(foundSourceFaithfulCliffGeometry) +
            ", fringe-field=" +
            std::to_string(foundContinuousFringeField) +
            ", fringe-phase=" +
            std::to_string(foundSourceFringeMaskPhase) +
            ", rebuilt-untouched-fringe=" +
            std::to_string(foundRebuiltUntouchedFringe) +
            ", concave-cliff=" +
            std::to_string(foundConcaveCliffJoin) +
            ", concave-fringe=" +
            std::to_string(foundConcaveFringeJoin) +
            ", concave-crown=" +
            std::to_string(foundConcaveCrownUnderlay) +
            ", lawn-corner-underlay=" +
            std::to_string(foundLawnCornerUnderlay) +
            ", convex-lawn-corner-underlay=" +
            std::to_string(foundConvexLawnCornerUnderlay) +
            ", convex-lawn-corner-pocket-repair=" +
            std::to_string(foundConvexLawnCornerPocketRepair) +
            ", convex-lawn-cap-underlay=" +
            std::to_string(foundConvexLawnCapUnderlay) +
            ", source-handoff-underlay=" +
            std::to_string(foundSourceHandoffUnderlay) +
            ", fringe-corner=" +
            std::to_string(foundFringeCorner) +
            ", cliff-corner=" +
            std::to_string(foundCliffCorner) +
            ", fringe-corner-field=" +
            std::to_string(foundAdvancingFringeCornerField) +
            ", cliff-corner-field=" +
            std::to_string(foundAdvancingCliffCornerField) +
            ", organic-cliff=" +
            std::to_string(foundInsetOrganicCliff) +
            ", organic-fringe=" +
            std::to_string(foundInsetOrganicFringe) +
            ", crown-overlap=" +
            std::to_string(foundFringeCrownSourcePlane) + ").";
        return false;
    }
    if (!foundLowerLawnLedgeOverlap ||
        !foundLowerLawnFootColorBlend ||
        !foundFlatLightLawnLedgeBase ||
        !foundNormalLightLawnOneRowFromLedge ||
        !flatLightLawnLedgeBaseRemainsPlanar ||
        retainedSourceGroundShelfAtLedgeBase ||
        !ledgeBaseGroundOverlayIndicesValid ||
        !foundDirtLawnFootColorBlend ||
        !foundDirtFootCoreColor ||
        !foundLowerLawnTerminalEdgeFill) {
        outFail =
            "The rebuilt lower Route 1 lawn did not tuck beneath the generated cliff foot, blend into its foliage Color0, and continue through the straight z=0 terminal edge (foot=" +
            std::to_string(foundLowerLawnLedgeOverlap) +
            ", color=" +
            std::to_string(foundLowerLawnFootColorBlend) +
            ", flat-light-lawn-base=" +
            std::to_string(foundFlatLightLawnLedgeBase) +
            ", normal-light-lawn-row=" +
            std::to_string(foundNormalLightLawnOneRowFromLedge) +
            ", planar-light-lawn-base=" +
            std::to_string(flatLightLawnLedgeBaseRemainsPlanar) +
            ", stale-source-ground-shelf=" +
            std::to_string(retainedSourceGroundShelfAtLedgeBase) +
            ", overlay-indices-valid=" +
            std::to_string(ledgeBaseGroundOverlayIndicesValid) +
            ", dirt-lawn-color=" +
            std::to_string(foundDirtLawnFootColorBlend) +
            ", dirt-core-color=" +
            std::to_string(foundDirtFootCoreColor) +
            ", terminal=" +
            std::to_string(foundLowerLawnTerminalEdgeFill) + ").";
        return false;
    }
    if (!foundUpperLawnCrownClip ||
        !foundUpperSourceLawnFields ||
        !foundNormalizedUpperLawnCrownClip ||
        !foundUpperLawnCrownNormal ||
        !foundNormalizedUpperLawnCrownNormal ||
        !foundNormalizedUpperLightLawnFields ||
        maximumUpperLawnCrownX > 1676.2f) {
        outFail =
            "The rebuilt upper Route 1 lawn did not meet the source crown contour while preserving both source-authentic and normalized light-lawn fields with no square overhang (found=" +
            std::to_string(foundUpperLawnCrownClip) +
            ", source-fields=" +
            std::to_string(foundUpperSourceLawnFields) +
            ", source-normal=" +
            std::to_string(foundUpperLawnCrownNormal) +
            ", normalized-light-fields=" +
            std::to_string(foundNormalizedUpperLightLawnFields) +
            ", normalized-crown=" +
            std::to_string(foundNormalizedUpperLawnCrownClip) +
            ", normalized-normal=" +
            std::to_string(foundNormalizedUpperLawnCrownNormal) +
            ", normalized-mid=" +
            std::to_string(normalizedMidCrownX) + "/" +
            std::to_string(normalizedMidCrownNormal[0]) + "," +
            std::to_string(normalizedMidCrownNormal[1]) + "," +
            std::to_string(normalizedMidCrownNormal[2]) +
            ", maximum-x=" +
            std::to_string(maximumUpperLawnCrownX) + ").";
        return false;
    }
    std::sort(
        upperLawnCrossSectionX.begin(),
        upperLawnCrossSectionX.end());
    upperLawnCrossSectionX.erase(
        std::unique(
            upperLawnCrossSectionX.begin(),
            upperLawnCrossSectionX.end(),
            [](float left, float right) {
                return std::abs(left - right) <= 0.01f;
            }),
        upperLawnCrossSectionX.end());
    float minimumUpperLawnSpacing =
        std::numeric_limits<float>::max();
    for (std::size_t index = 1u;
         index < upperLawnCrossSectionX.size();
         ++index) {
        minimumUpperLawnSpacing = std::min(
            minimumUpperLawnSpacing,
            upperLawnCrossSectionX[index] -
                upperLawnCrossSectionX[index - 1u]);
    }
    if (upperLawnCrossSectionX.size() < 15u ||
        minimumUpperLawnSpacing < 2.0f) {
        outFail =
            "The rebuilt upper Route 1 cap collapsed multiple textured grid columns into the crown ribbon (samples=" +
            std::to_string(upperLawnCrossSectionX.size()) +
            ", minimum-spacing=" +
            std::to_string(minimumUpperLawnSpacing) + ").";
        return false;
    }
    if (maximumLowerLawnTerminalContactZ < -0.01f ||
        maximumLowerLawnTerminalContactZ > 0.01f) {
        outFail =
            "The lower Route 1 lawn did not follow its neighboring straight cliff through the complete z=0 terminal endpoint (maximum-z=" +
            std::to_string(maximumLowerLawnTerminalContactZ) + ").";
        return false;
    }
    // The decoded material-18 and material-13 source carriers are authored as
    // separate, nearly coincident strips. Across the retained source-local
    // edge set their independent extreme samples differ by up to roughly
    // 0.31 cm even though corresponding rows raster as one boundary, so keep
    // a sub-half-centimetre source tolerance rather than demanding synthetic
    // bit identity from unrelated extrema.
    constexpr float kSourceCrownCarrierToleranceCm = 0.35f;
    if (std::abs(
            minimumFullCliffCrownOutward -
            minimumFullFringeCrownOutward) >
            kSourceCrownCarrierToleranceCm ||
        std::abs(
            maximumFullCliffCrownOutward -
            maximumFullFringeCrownOutward) >
            kSourceCrownCarrierToleranceCm) {
        outFail =
            "The rebuilt Route 1 cliff and leafy carrier no longer share one measured crown contour (cliff=" +
            std::to_string(minimumFullCliffCrownOutward) + ".." +
            std::to_string(maximumFullCliffCrownOutward) +
            ", fringe=" +
            std::to_string(minimumFullFringeCrownOutward) + ".." +
            std::to_string(maximumFullFringeCrownOutward) + ").";
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
    auto cornerContinuationLayout = loweredLawnLayout;
    cornerContinuationLayout.authoredTerrainTiles.push_back(
        authoredTileFromSource(17, -3, 0, "light_lawn", "auto"));
    cornerContinuationLayout.authoredTerrainTiles.push_back(
        authoredTileFromSource(17, -4, 0, "light_lawn", "auto"));
    if (!environment.applyBoardLayout(
            cornerContinuationLayout, &error)) {
        outFail =
            "The paired Route 1 corner-continuation fixture was rejected: " +
            error;
        return false;
    }
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        cornerContinuationBatches;
    environment.appendIndexedBatches(
        0.0f, cornerContinuationBatches);
    bool foundWestSourceSideContactSurface = false;
    bool foundEastSourceSideContactSurface = false;
    bool foundWestDiagonalCornerContactSurface = false;
    bool foundEastDiagonalCornerContactSurface = false;
    bool foundWestRaisedCornerFloor = false;
    bool foundEastRaisedCornerFloor = false;
    bool retainedInvalidatedSourceCornerCarrier = false;
    bool foundTile16SouthCrownUnderlay = false;
    double maximumWestCornerGroundEdgeCm = 0.0;
    double maximumEastCornerGroundEdgeCm = 0.0;
    const auto containsXZ = [](
                                const std::array<
                                    std::array<double, 3>, 3>& triangle,
                                double x,
                                double z) {
        const auto side = [](const auto& first,
                             const auto& second,
                             double pointX,
                             double pointZ) {
            return (second[0] - first[0]) *
                    (pointZ - first[2]) -
                (second[2] - first[2]) *
                    (pointX - first[0]);
        };
        constexpr double tolerance = 0.001;
        const std::array<double, 3> sides{
            side(triangle[0], triangle[1], x, z),
            side(triangle[1], triangle[2], x, z),
            side(triangle[2], triangle[0], x, z)};
        const bool hasNegative = std::any_of(
            sides.begin(), sides.end(), [](double value) {
                return value < -tolerance;
            });
        const bool hasPositive = std::any_of(
            sides.begin(), sides.end(), [](double value) {
                return value > tolerance;
            });
        return !(hasNegative && hasPositive);
    };
    // A handful of interior points can pass while the rounded contact still
    // exposes large wedges near its outer grid corner. Rasterize the complete
    // three-cell low-side footprint using the same per-instance transforms as
    // the renderer and require ground coverage throughout it.
    std::vector<std::array<std::array<double, 3>, 3>>
        eastCornerGroundTriangles;
    bool foundEastConcaveDiagonalCrown = false;
    bool retainedEastConcaveSourceCrown = false;
    for (const auto& batch : cornerContinuationBatches) {
        foundTile16SouthCrownUnderlay =
            foundTile16SouthCrownUnderlay ||
            batch.geometryCacheKey.find(
                "route1:terrain-ledge-crown-underlay:cell-16--4:edge-2:") !=
                std::string::npos;
        const bool authoredSurface =
            batch.geometryCacheKey.find(
                "route1:terrain-authored-surface:") !=
            std::string::npos;
        const bool sourceGroundCarrier =
            batch.geometryCacheKey.find("mesh:35:group:0") !=
                std::string::npos &&
            batch.geometryCacheKey.find(":terrain-mask:") !=
                std::string::npos;
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const std::size_t vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        const auto* indices = batch.sharedIndices
            ? batch.sharedIndices
            : batch.indices.data();
        const std::size_t indexCount = batch.sharedIndices
            ? batch.sharedIndexCount
            : batch.indices.size();
        const auto appendInstance = [&](const auto& matrix) {
            for (std::size_t index = 0u;
                 index + 2u < indexCount;
                 index += 3u) {
                if (indices[index] >= vertexCount ||
                    indices[index + 1u] >= vertexCount ||
                    indices[index + 2u] >= vertexCount) {
                    continue;
                }
                std::array<std::array<double, 3>, 3> points{};
                bool ground = true;
                double minimumX =
                    std::numeric_limits<double>::max();
                double maximumX =
                    std::numeric_limits<double>::lowest();
                double minimumZ =
                    std::numeric_limits<double>::max();
                double maximumZ =
                    std::numeric_limits<double>::lowest();
                double centerX = 0.0;
                double centerZ = 0.0;
                bool raisedGround = true;
                for (std::size_t corner = 0u;
                     corner < 3u;
                     ++corner) {
                    const auto& vertex =
                        vertices[indices[index + corner]];
                    const auto worldPoint = transformPoint(
                        matrix,
                        {vertex.x, vertex.y, vertex.z});
                    points[corner] = transformPoint(
                        sourceFromWorld, worldPoint);
                    ground = ground &&
                        points[corner][1] >= -1.0 &&
                        points[corner][1] <= 1.0;
                    raisedGround = raisedGround &&
                        points[corner][1] >= 45.0 &&
                        points[corner][1] <= 55.0;
                    centerX += points[corner][0] / 3.0;
                    centerZ += points[corner][2] / 3.0;
                    minimumX = std::min(
                        minimumX, points[corner][0]);
                    maximumX = std::max(
                        maximumX, points[corner][0]);
                    minimumZ = std::min(
                        minimumZ, points[corner][2]);
                    maximumZ = std::max(
                        maximumZ, points[corner][2]);
                }
                if (raisedGround && authoredSurface &&
                    containsXZ(points, 2850.0, -350.0)) {
                    foundEastConcaveDiagonalCrown = true;
                }
                if (raisedGround && sourceGroundCarrier &&
                    centerX > 2800.0 && centerX < 2900.0 &&
                    centerZ > -400.0 && centerZ < -300.0) {
                    retainedEastConcaveSourceCrown = true;
                }
                if (ground && maximumX >= 2399.0 &&
                    minimumX <= 2601.0 &&
                    maximumZ >= -501.0 &&
                    minimumZ <= -299.0) {
                    eastCornerGroundTriangles.push_back(points);
                }
            }
        };
        if (batch.instances.empty()) {
            appendInstance(batch.modelMatrix);
        } else {
            for (const auto& instance : batch.instances) {
                appendInstance(instance.modelMatrix);
            }
        }
    }
    if (!foundTile16SouthCrownUnderlay) {
        outFail =
            "The rebuilt Route 1 ledge from (16, -4) toward (16, -5) lost its opaque crown underlay.";
        return false;
    }
    std::size_t uncoveredEastCornerSamples = 0u;
    std::array<double, 2> firstUncoveredEastCorner{};
    for (double z = -499.0; z <= -301.0; z += 2.0) {
        for (double x = 2401.0; x <= 2599.0; x += 2.0) {
            const bool highCell = x > 2500.0 && z > -400.0;
            if (highCell) {
                continue;
            }
            const bool covered = std::any_of(
                eastCornerGroundTriangles.begin(),
                eastCornerGroundTriangles.end(),
                [&](const auto& triangle) {
                    return containsXZ(triangle, x, z);
                });
            if (!covered) {
                if (uncoveredEastCornerSamples == 0u) {
                    firstUncoveredEastCorner = {x, z};
                }
                ++uncoveredEastCornerSamples;
            }
        }
    }
    if (uncoveredEastCornerSamples != 0u) {
        outFail =
            "The Route 1 east convex corner leaves " +
            std::to_string(uncoveredEastCornerSamples) +
            " uncovered low-ground samples; first gap is at (" +
            std::to_string(firstUncoveredEastCorner[0]) + "," +
            std::to_string(firstUncoveredEastCorner[1]) + ").";
        return false;
    }
    if (foundEastConcaveDiagonalCrown ||
        !retainedEastConcaveSourceCrown) {
        outFail =
            "The Route 1 east concave handoff replaced a diagonally adjacent, uninvolved source cap instead of keeping it authoritative (generated=" +
            std::to_string(foundEastConcaveDiagonalCrown) +
            ", retained-source=" +
            std::to_string(retainedEastConcaveSourceCrown) + ").";
        return false;
    }
    for (const auto& batch : cornerContinuationBatches) {
        const bool authoredSurface =
            batch.geometryCacheKey.find(
                "route1:terrain-authored-surface:") !=
            std::string::npos;
        const bool sourceCornerCarrier =
            batch.geometryCacheKey.find("mesh:31:group:1") !=
                std::string::npos &&
            batch.geometryCacheKey.find(":terrain-mask:") !=
                std::string::npos;
        if (!authoredSurface && !sourceCornerCarrier) {
            continue;
        }
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const std::size_t vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        const auto* indices = batch.sharedIndices
            ? batch.sharedIndices
            : batch.indices.data();
        const std::size_t indexCount = batch.sharedIndices
            ? batch.sharedIndexCount
            : batch.indices.size();
        if (authoredSurface) {
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < vertexCount;
                 ++vertexIndex) {
                const auto& vertex = vertices[vertexIndex];
                const auto point = transformPoint(
                    batch.modelMatrix,
                    {vertex.x, vertex.y, vertex.z});
                foundWestSourceSideContactSurface =
                    foundWestSourceSideContactSurface ||
                    (std::abs(point[0] - 1650.0) <= 0.01 &&
                     std::abs(point[1] - 0.02) <= 0.01 &&
                     std::abs(point[2] + 450.0) <= 0.01);
                foundEastSourceSideContactSurface =
                    foundEastSourceSideContactSurface ||
                    (std::abs(point[0] - 2550.0) <= 0.01 &&
                     std::abs(point[1] - 0.02) <= 0.01 &&
                     std::abs(point[2] + 450.0) <= 0.01);
            }
        }
        for (std::size_t index = 0u; index + 2u < indexCount; index += 3u) {
            if (indices[index] >= vertexCount ||
                indices[index + 1u] >= vertexCount ||
                indices[index + 2u] >= vertexCount) {
                continue;
            }
            std::array<std::array<double, 3>, 3> points{};
            double centerX = 0.0;
            double centerY = 0.0;
            double centerZ = 0.0;
            double maximumEdge = 0.0;
            for (std::size_t corner = 0u; corner < 3u; ++corner) {
                const auto& vertex = vertices[indices[index + corner]];
                points[corner] = transformPoint(
                    batch.modelMatrix,
                    {vertex.x, vertex.y, vertex.z});
                centerX += points[corner][0] / 3.0;
                centerY += points[corner][1] / 3.0;
                centerZ += points[corner][2] / 3.0;
            }
            for (std::size_t first = 0u; first < 3u; ++first) {
                const std::size_t second = (first + 1u) % 3u;
                maximumEdge = std::max(
                    maximumEdge,
                    std::hypot(
                        points[first][0] - points[second][0],
                        points[first][2] - points[second][2]));
            }
            const bool withinRebuiltCorner =
                ((centerX >= 1635.0 && centerX <= 1765.0) ||
                 (centerX >= 2435.0 && centerX <= 2565.0)) &&
                centerY >= -5.0 && centerY <= 60.0 &&
                centerZ >= -465.0 && centerZ <= -335.0;
            if (sourceCornerCarrier && withinRebuiltCorner) {
                retainedInvalidatedSourceCornerCarrier = true;
            }
            if (!authoredSurface) {
                continue;
            }
            const bool lowContactTriangle = std::all_of(
                points.begin(), points.end(), [](const auto& point) {
                    return point[1] >= -5.0 && point[1] <= 5.0;
                });
            if (lowContactTriangle) {
                foundWestDiagonalCornerContactSurface =
                    foundWestDiagonalCornerContactSurface ||
                    containsXZ(points, 1720.0, -420.0);
                foundEastDiagonalCornerContactSurface =
                    foundEastDiagonalCornerContactSurface ||
                    containsXZ(points, 2480.0, -420.0);
            }
            const bool coversWestCorner =
                containsXZ(points, 1660.0, -360.0);
            const bool coversEastCorner =
                containsXZ(points, 2540.0, -360.0);
            const bool raisedFloorTriangle = std::all_of(
                points.begin(), points.end(), [](const auto& point) {
                    return point[1] >= 40.0 && point[1] <= 60.0;
                });
            if (raisedFloorTriangle) {
                foundWestRaisedCornerFloor =
                    foundWestRaisedCornerFloor ||
                    coversWestCorner;
                foundEastRaisedCornerFloor =
                    foundEastRaisedCornerFloor ||
                    coversEastCorner;
            }
            if (centerY < -1.0 || centerY > 1.0) {
                continue;
            }
            if (std::abs(centerX - 1700.0) <= 65.0 &&
                std::abs(centerZ + 400.0) <= 65.0) {
                maximumWestCornerGroundEdgeCm = std::max(
                    maximumWestCornerGroundEdgeCm,
                    maximumEdge);
            }
            if (std::abs(centerX - 2500.0) <= 65.0 &&
                std::abs(centerZ + 400.0) <= 65.0) {
                maximumEastCornerGroundEdgeCm = std::max(
                    maximumEastCornerGroundEdgeCm,
                    maximumEdge);
            }
        }
    }
    const auto submitted = [&](std::string_view prefix) {
        return std::any_of(
            cornerContinuationBatches.begin(),
            cornerContinuationBatches.end(),
            [&](const auto& batch) {
                return batch.geometryCacheKey.starts_with(prefix);
            });
    };
    const bool foundWestCornerCliffContinuation = submitted(
        "route1:terrain-cliff:cell-16--4:edge-1:");
    const bool foundWestCornerFringeContinuation = submitted(
        "route1:terrain-fringe:cell-16--4:edge-1:");
    const bool foundEastCornerCliffContinuation = submitted(
        "route1:terrain-cliff:cell-25--4:edge-2:");
    const bool foundEastCornerFringeContinuation = submitted(
        "route1:terrain-fringe:cell-25--4:edge-2:");
    const bool foundEastStraightCliffContinuation = submitted(
        "route1:terrain-cliff:cell-26--4:edge-2:");
    const bool foundEastStraightFringeContinuation = submitted(
        "route1:terrain-fringe:cell-26--4:edge-2:");
    const bool foundEastHighCapUnderlay = submitted(
        "route1:terrain-convex-lawn-cap-underlay:cell-25--4:corner-2:level-1:");
    const bool foundWestHighCapUnderlay = submitted(
        "route1:terrain-convex-lawn-cap-underlay:cell-16--4:corner-1:level-1:");
    const bool foundWestRaisedSourceHandoff =
        submitted(
            "route1:terrain-source-handoff-underlay:cell-15--4:neighbor-16--4:edge-1:level-1:") ||
        submitted(
            "route1:terrain-source-handoff-underlay:cell-16--4:neighbor-15--4:edge-3:level-1:");
    const bool foundEastFirstPocketHalf = submitted(
        "route1:terrain-convex-lawn-corner-pocket-repair:cell-25--5:x-2500:z--400:half-0:level-0:");
    const bool foundEastSecondPocketHalf = submitted(
        "route1:terrain-convex-lawn-corner-pocket-repair:cell-24--4:x-2500:z--400:half-1:level-0:");
    const bool foundEastLowSourceHandoff =
        submitted(
            "route1:terrain-source-handoff-underlay:cell-24--5:neighbor-24--4:edge-0:level-0:") ||
        submitted(
            "route1:terrain-source-handoff-underlay:cell-24--4:neighbor-24--5:edge-2:level-0:");
    struct RaisedCapBoundarySample {
        std::array<double, 3> point{};
        std::array<float, 15> attributes{};
    };
    std::vector<RaisedCapBoundarySample> eastRaisedCapBoundary;
    for (const auto& batch : cornerContinuationBatches) {
        const bool authoredSurface = batch.geometryCacheKey.find(
            "route1:terrain-authored-surface:") != std::string::npos;
        if (!authoredSurface) {
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
            const auto point = transformPoint(
                batch.modelMatrix,
                {vertex.x, vertex.y, vertex.z});
            const bool withinRaisedCapBand =
                point[1] >= 40.0 && point[1] <= 60.0 &&
                point[2] >= -400.01 && point[2] <= -299.99;
            if (withinRaisedCapBand &&
                // Source-authored crown endpoints wander slightly from the
                // logical x=2600 grid line. Compare the two generated caps at
                // their shared decoded positions, not only at the synthetic
                // integer boundary.
                std::abs(point[0] - 2600.0) <= 5.0) {
                eastRaisedCapBoundary.push_back({
                    .point = point,
                    .attributes = {
                        vertex.u,
                        vertex.v,
                        vertex.r,
                        vertex.g,
                        vertex.b,
                        vertex.a,
                        vertex.nx,
                        vertex.ny,
                        vertex.nz,
                        vertex.sourceUv1U,
                        vertex.sourceUv1V,
                        vertex.sourceUv2U,
                        vertex.sourceUv2V,
                        vertex.tx,
                        vertex.tw}});
            }
        }
    }
    std::size_t sharedEastRaisedCapVertices = 0u;
    bool mismatchedEastRaisedCapAttributes = false;
    for (std::size_t first = 0u;
         first < eastRaisedCapBoundary.size();
         ++first) {
        for (std::size_t second = first + 1u;
             second < eastRaisedCapBoundary.size();
             ++second) {
            const auto& lhs = eastRaisedCapBoundary[first];
            const auto& rhs = eastRaisedCapBoundary[second];
            if (std::abs(lhs.point[0] - rhs.point[0]) > 0.10 ||
                std::abs(lhs.point[1] - rhs.point[1]) > 0.01 ||
                std::abs(lhs.point[2] - rhs.point[2]) > 0.01) {
                continue;
            }
            ++sharedEastRaisedCapVertices;
            for (std::size_t attribute = 0u;
                 attribute < lhs.attributes.size();
                 ++attribute) {
                if (std::abs(
                        lhs.attributes[attribute] -
                        rhs.attributes[attribute]) > 0.001f) {
                    mismatchedEastRaisedCapAttributes = true;
                    break;
                }
            }
        }
    }
    if (!foundWestCornerCliffContinuation ||
        !foundWestCornerFringeContinuation ||
        !foundEastCornerCliffContinuation ||
        !foundEastCornerFringeContinuation ||
        !foundWestHighCapUnderlay ||
        !foundWestRaisedSourceHandoff ||
        !foundEastHighCapUnderlay ||
        !foundEastFirstPocketHalf ||
        !foundEastSecondPocketHalf ||
        !foundEastLowSourceHandoff ||
        foundEastStraightCliffContinuation ||
        foundEastStraightFringeContinuation ||
        !foundWestSourceSideContactSurface ||
        !foundEastSourceSideContactSurface ||
        !foundWestDiagonalCornerContactSurface ||
        !foundEastDiagonalCornerContactSurface ||
        !foundWestRaisedCornerFloor ||
        !foundEastRaisedCornerFloor ||
        mismatchedEastRaisedCapAttributes ||
        retainedInvalidatedSourceCornerCarrier ||
        maximumWestCornerGroundEdgeCm > 15.0 ||
        maximumEastCornerGroundEdgeCm > 15.0) {
        outFail =
            "The Route 1 corners at (16,-4) and (25,-4) did not confine rebuilt cliff/fringe carriers to the affected turns, preserve the straight source continuation, or cover both low-side ground contacts (west-cliff=" +
            std::to_string(foundWestCornerCliffContinuation) +
            ", west-fringe=" +
            std::to_string(foundWestCornerFringeContinuation) +
            ", east-cliff=" +
            std::to_string(foundEastCornerCliffContinuation) +
            ", east-fringe=" +
            std::to_string(foundEastCornerFringeContinuation) +
            ", west-high-cap-underlay=" +
            std::to_string(foundWestHighCapUnderlay) +
            ", west-raised-source-handoff=" +
            std::to_string(foundWestRaisedSourceHandoff) +
            ", east-high-cap-underlay=" +
            std::to_string(foundEastHighCapUnderlay) +
            ", east-first-pocket-half=" +
            std::to_string(foundEastFirstPocketHalf) +
            ", east-second-pocket-half=" +
            std::to_string(foundEastSecondPocketHalf) +
            ", east-low-source-handoff=" +
            std::to_string(foundEastLowSourceHandoff) +
            ", east-straight-cliff=" +
            std::to_string(foundEastStraightCliffContinuation) +
            ", east-straight-fringe=" +
            std::to_string(foundEastStraightFringeContinuation) +
            ", west-source-contact=" +
            std::to_string(foundWestSourceSideContactSurface) +
            ", east-source-contact=" +
            std::to_string(foundEastSourceSideContactSurface) +
            ", west-diagonal-contact=" +
            std::to_string(foundWestDiagonalCornerContactSurface) +
            ", east-diagonal-contact=" +
            std::to_string(foundEastDiagonalCornerContactSurface) +
            ", west-raised-floor=" +
            std::to_string(foundWestRaisedCornerFloor) +
            ", east-raised-floor=" +
            std::to_string(foundEastRaisedCornerFloor) +
            ", east-shared-cap-vertices=" +
            std::to_string(sharedEastRaisedCapVertices) +
            ", east-cap-attribute-mismatch=" +
            std::to_string(mismatchedEastRaisedCapAttributes) +
            ", stale-source-carrier=" +
            std::to_string(retainedInvalidatedSourceCornerCarrier) +
            ", west-max-ground-edge-cm=" +
            std::to_string(maximumWestCornerGroundEdgeCm) +
            ", east-max-ground-edge-cm=" +
            std::to_string(maximumEastCornerGroundEdgeCm) + ").";
        return false;
    }
    if (!environment.setTerrainPatchV2PreviewEnabled(true, &error)) {
        outFail =
            "Terrain Patch V2 rejected the cooked corner-continuation fixture: " +
            error;
        return false;
    }
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        regionalCornerBatches;
    environment.appendIndexedBatches(
        0.0f,
        regionalCornerBatches);
    const auto regionalSubmitted = [&](std::string_view fragment) {
        return std::any_of(
            regionalCornerBatches.begin(),
            regionalCornerBatches.end(),
            [&](const auto& batch) {
                return batch.geometryCacheKey.find(fragment) !=
                    std::string::npos;
            });
    };
    const bool retainedLegacyWideUnderlay =
        regionalSubmitted("terrain-ledge-crown-underlay:") ||
        regionalSubmitted(
            "terrain-ledge-crown-contour-underlay:cell-16--4:") ||
        regionalSubmitted(
            "terrain-ledge-crown-contour-underlay:cell-25--4:") ||
        regionalSubmitted(
            "terrain-convex-crown-contour-underlay:cell-16--4:") ||
        regionalSubmitted(
            "terrain-convex-crown-contour-underlay:cell-25--4:") ||
        regionalSubmitted(
            "terrain-convex-foot-contour-underlay:cell-16--4:") ||
        regionalSubmitted(
            "terrain-convex-foot-contour-underlay:cell-25--4:") ||
        regionalSubmitted("terrain-convex-lawn-corner-underlay:") ||
        regionalSubmitted("terrain-convex-lawn-corner-pocket-repair:") ||
        regionalSubmitted("terrain-convex-lawn-cap-underlay:") ||
        regionalSubmitted("terrain-lawn-corner-underlay:") ||
        regionalSubmitted("terrain-source-handoff-underlay:");
    bool contourEnvelopeValid = true;
    bool contourTopologyValid = true;
    bool contourContactFieldValid = true;
    std::size_t regionalCrownCount = 0u;
    std::size_t regionalFootCount = 0u;
    float maximumRegionalFringeUvDelta = 0.0f;
    float maximumRegionalFringeEdgeCm = 0.0f;
    for (const auto& batch : regionalCornerBatches) {
        const bool regionalCrown =
            batch.geometryCacheKey.find(
                "terrain-regional-crown-contour-underlay:") !=
                std::string::npos;
        const bool regionalFoot =
            batch.geometryCacheKey.find(
                "terrain-regional-convex-foot-pocket:") !=
                std::string::npos;
        const bool regionalFringe =
            batch.geometryCacheKey.find(
                "terrain-regional-fringe-contour:") !=
                std::string::npos;
        if (regionalFringe) {
            const auto* fringeVertices = batch.sharedVertices
                ? batch.sharedVertices
                : batch.vertices.data();
            const std::size_t fringeVertexCount = batch.sharedVertices
                ? batch.sharedVertexCount
                : batch.vertices.size();
            const std::size_t rowWidth = fringeVertexCount / 3u;
            const bool closed =
                batch.geometryCacheKey.ends_with(":closed");
            const std::size_t segments = closed
                ? rowWidth
                : rowWidth > 0u ? rowWidth - 1u : 0u;
            for (std::size_t row = 0u; row < 3u; ++row) {
                for (std::size_t sample = 0u;
                     fringeVertices && sample < segments;
                     ++sample) {
                    const auto& left = fringeVertices[
                        row * rowWidth + sample];
                    const auto& right = fringeVertices[
                        row * rowWidth + (sample + 1u) % rowWidth];
                    maximumRegionalFringeUvDelta = std::max(
                        maximumRegionalFringeUvDelta,
                        std::abs(right.sourceUv1U - left.sourceUv1U));
                    const float dx = right.x - left.x;
                    const float dz = right.z - left.z;
                    maximumRegionalFringeEdgeCm = std::max(
                        maximumRegionalFringeEdgeCm,
                        std::sqrt(dx * dx + dz * dz));
                }
            }
        }
        if (!regionalCrown && !regionalFoot) {
            continue;
        }
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const std::size_t vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        const std::size_t indexCount = batch.sharedIndices
            ? batch.sharedIndexCount
            : batch.indices.size();
        if (!vertices || vertexCount == 0u) {
            contourEnvelopeValid = false;
            continue;
        }
        const auto* indices = batch.sharedIndices
            ? batch.sharedIndices
            : batch.indices.data();
        float maximumTriangleEdgeCm = 0.0f;
        for (std::size_t index = 0u;
             indices && index + 2u < indexCount;
             index += 3u) {
            const std::array triangle{
                indices[index], indices[index + 1u],
                indices[index + 2u]};
            if (triangle[0u] >= vertexCount ||
                triangle[1u] >= vertexCount ||
                triangle[2u] >= vertexCount) {
                contourTopologyValid = false;
                continue;
            }
            for (std::size_t edge = 0u; edge < 3u; ++edge) {
                const auto& first = vertices[triangle[edge]];
                const auto& second = vertices[triangle[(edge + 1u) % 3u]];
                const float deltaX = second.x - first.x;
                const float deltaZ = second.z - first.z;
                maximumTriangleEdgeCm = std::max(
                    maximumTriangleEdgeCm,
                    std::sqrt(deltaX * deltaX + deltaZ * deltaZ));
            }
        }
        if (regionalCrown) {
            ++regionalCrownCount;
            const bool closed = batch.geometryCacheKey.ends_with(":closed");
            const std::size_t rowWidth = vertexCount / 2u;
            const std::size_t segmentCount = closed
                ? rowWidth
                : rowWidth > 0u ? rowWidth - 1u : 0u;
            contourEnvelopeValid = contourEnvelopeValid &&
                maximumTriangleEdgeCm <= 12.0f;
            contourTopologyValid = contourTopologyValid &&
                vertexCount >= 4u && vertexCount % 2u == 0u &&
                indexCount == segmentCount * 6u;
            const std::size_t outerVertexCount = rowWidth;
            if (vertexCount < outerVertexCount) {
                contourContactFieldValid = false;
            } else {
                for (std::size_t vertexIndex = 0u;
                     vertexIndex < outerVertexCount;
                     ++vertexIndex) {
                    const auto& vertex = vertices[vertexIndex];
                    contourContactFieldValid =
                        contourContactFieldValid &&
                        std::abs(vertex.r - 0.180392161f) <=
                            0.001f &&
                        std::abs(vertex.g - 0.482352942f) <=
                            0.001f &&
                        std::abs(vertex.b - 0.431372553f) <=
                            0.001f &&
                        std::abs(vertex.ny - 0.998535156f) <=
                            0.001f;
                }
            }
        } else {
            ++regionalFootCount;
            contourEnvelopeValid = contourEnvelopeValid &&
                maximumTriangleEdgeCm <= 8.0f;
            contourTopologyValid = contourTopologyValid &&
                vertexCount >= 3u && indexCount >= 3u;
        }
    }
    const bool foundWestFootContour = regionalSubmitted(
        "terrain-regional-convex-foot-pocket:cell-16--4:corner-1:");
    const bool foundEastFootContour = regionalSubmitted(
        "terrain-regional-convex-foot-pocket:cell-25--4:corner-2:");
    const bool foundRegionalCliff = regionalSubmitted(
        "terrain-regional-cliff-contour:");
    const bool foundRegionalFringe = regionalSubmitted(
        "terrain-regional-fringe-contour:");
    if (retainedLegacyWideUnderlay ||
        !contourEnvelopeValid ||
        !contourTopologyValid ||
        !contourContactFieldValid ||
        regionalCrownCount == 0u ||
        regionalFootCount == 0u ||
        !foundWestFootContour ||
        !foundEastFootContour ||
        !foundRegionalCliff ||
        !foundRegionalFringe ||
        maximumRegionalFringeUvDelta > 0.05f ||
        maximumRegionalFringeEdgeCm > 6.0f ||
        !regionalSubmitted("route1:terrain-authored-surface:")) {
        outFail =
            "Terrain Patch V2 must submit its connected surface, bounded straight/corner contour gaskets, and source-profile cliff/fringe without the legacy rectangular crown/corner repair meshes (legacy=" +
            std::to_string(retainedLegacyWideUnderlay) +
            ", envelope=" + std::to_string(contourEnvelopeValid) +
            ", topology=" + std::to_string(contourTopologyValid) +
            ", contact-field=" +
            std::to_string(contourContactFieldValid) +
            ", regional-crowns=" +
            std::to_string(regionalCrownCount) +
            ", regional-feet=" +
            std::to_string(regionalFootCount) +
            ", west-foot=" + std::to_string(foundWestFootContour) +
            ", east-foot=" +
            std::to_string(foundEastFootContour) +
            ", regional-cliff=" +
            std::to_string(foundRegionalCliff) +
            ", regional-fringe=" +
            std::to_string(foundRegionalFringe) +
            ", fringe-max-du=" +
            std::to_string(maximumRegionalFringeUvDelta) +
            ", fringe-max-edge-cm=" +
            std::to_string(maximumRegionalFringeEdgeCm) + ").";
        return false;
    }
    auto regionalMaterialHandoffLayout = environment.layout();
    std::erase_if(
        regionalMaterialHandoffLayout.authoredTerrainTiles,
        [](const route1::AuthoredTerrainTile& tile) {
            return tile.gridX == 22 &&
                (tile.gridZ == -11 || tile.gridZ == -10);
        });
    auto regionalLawnSocket = authoredTileFromSource(
        22, -10, 1, "light_lawn", "auto");
    regionalLawnSocket.reason =
        "terrain_regional_material_socket_lawn_regression";
    regionalMaterialHandoffLayout.authoredTerrainTiles.push_back(
        std::move(regionalLawnSocket));
    auto regionalDirtSocket = authoredTileFromSource(
        22, -11, 1, "dirt_path", "path_10");
    regionalDirtSocket.reason =
        "terrain_regional_material_handoff_regression";
    regionalMaterialHandoffLayout.authoredTerrainTiles.push_back(
        std::move(regionalDirtSocket));
    if (!environment.applyBoardLayout(
            regionalMaterialHandoffLayout, &error) ||
        !environment.setTerrainPatchV2PreviewEnabled(true, &error)) {
        outFail =
            "Terrain Patch V2 rejected the generated-dirt/source-lawn handoff fixture: " +
            error;
        return false;
    }
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        regionalMaterialHandoffBatches;
    environment.appendIndexedBatches(
        0.0f,
        regionalMaterialHandoffBatches);
    const auto regionalLawnTile = std::find_if(
        environment.terrainTiles().begin(),
        environment.terrainTiles().end(),
        [](const route1::TerrainTileState& tile) {
            return tile.gridX == 22 && tile.gridZ == -10;
        });
    bool foundGeneratedLawnSocketTriangle = false;
    bool retainedSourceLawnSocketTriangle = false;
    for (const auto& batch : regionalMaterialHandoffBatches) {
        const bool generatedSurface =
            batch.geometryCacheKey.find(
                "route1:terrain-authored-surface:") !=
            std::string::npos;
        const auto materialIndex = batch.sharedTemplate
            ? batch.sharedTemplate->sourceMaterialIndex
            : batch.sourceMaterialIndex;
        const bool sourceGround = materialIndex == 19u &&
            batch.geometryCacheKey.starts_with(
                "published-environment:");
        if (!generatedSurface && !sourceGround) {
            continue;
        }
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const auto vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        const auto* indices = batch.sharedIndices
            ? batch.sharedIndices
            : batch.indices.data();
        const auto indexCount = batch.sharedIndices
            ? batch.sharedIndexCount
            : batch.indices.size();
        for (const auto& instance : batch.instances) {
            for (std::size_t index = 0u;
                 vertices && indices && index + 2u < indexCount;
                 index += 3u) {
                std::array<std::array<double, 3>, 3> points{};
                bool valid = true;
                for (std::size_t corner = 0u;
                     corner < points.size();
                     ++corner) {
                    const auto vertexIndex = indices[index + corner];
                    if (vertexIndex >= vertexCount) {
                        valid = false;
                        break;
                    }
                    const auto& vertex = vertices[vertexIndex];
                    const auto worldPoint = transformPoint(
                        instance.modelMatrix,
                        {vertex.x, vertex.y, vertex.z});
                    points[corner] = transformPoint(
                        sourceFromWorld,
                        worldPoint);
                }
                if (!valid) {
                    continue;
                }
                const double centerX =
                    (points[0][0] + points[1][0] + points[2][0]) /
                    3.0;
                const double centerZ =
                    (points[0][2] + points[1][2] + points[2][2]) /
                    3.0;
                if (static_cast<std::int32_t>(std::floor(
                        centerX / 100.0)) != 22 ||
                    static_cast<std::int32_t>(std::floor(
                        centerZ / 100.0)) != -10) {
                    continue;
                }
                foundGeneratedLawnSocketTriangle =
                    foundGeneratedLawnSocketTriangle ||
                    generatedSurface;
                retainedSourceLawnSocketTriangle =
                    retainedSourceLawnSocketTriangle || sourceGround;
            }
        }
    }
    if (regionalLawnTile == environment.terrainTiles().end() ||
        !regionalLawnTile->rebuildContinuousMaterialFields ||
        !foundGeneratedLawnSocketTriangle ||
        retainedSourceLawnSocketTriangle) {
        outFail =
            "Terrain Patch V2 did not replace a stale source dirt/lawn selector with one continuous generated lawn socket (field=" +
            std::to_string(
                regionalLawnTile != environment.terrainTiles().end() &&
                regionalLawnTile->rebuildContinuousMaterialFields) +
            ", generated=" +
            std::to_string(foundGeneratedLawnSocketTriangle) +
            ", retained-source=" +
            std::to_string(retainedSourceLawnSocketTriangle) + ").";
        return false;
    }
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        canonicalBenchReferenceBatches;
    environment.appendIndexedBatches(
        0.0f, canonicalBenchReferenceBatches);
    // appendIndexedBatches intentionally exposes registry-backed pointers for
    // the runtime fast path. The fixture mutates the environment below, which
    // can reallocate that registry; retain an owned diagnostic snapshot rather
    // than dereferencing stale shared geometry/material pointers afterward.
    for (auto& batch : canonicalBenchReferenceBatches) {
        if (batch.sharedTemplate) {
            batch.sourceMaterialIndex =
                batch.sharedTemplate->sourceMaterialIndex;
            batch.sharedTemplate = nullptr;
        }
        if (batch.sharedVertices && batch.sharedVertexCount > 0u) {
            batch.vertices.assign(
                batch.sharedVertices,
                batch.sharedVertices + batch.sharedVertexCount);
            batch.sharedVertices = nullptr;
            batch.sharedVertexCount = 0u;
        }
        if (batch.sharedIndices && batch.sharedIndexCount > 0u) {
            batch.indices.assign(
                batch.sharedIndices,
                batch.sharedIndices + batch.sharedIndexCount);
            batch.sharedIndices = nullptr;
            batch.sharedIndexCount = 0u;
        }
    }
    auto benchCornerLayout = environment.layout();
    std::erase_if(
        benchCornerLayout.authoredTerrainTiles,
        [](const route1::AuthoredTerrainTile& tile) {
            return tile.gridX >= 16 && tile.gridX <= 19 &&
                tile.gridZ >= -12 && tile.gridZ <= -11;
        });
    for (std::int32_t gridX = 16; gridX <= 19; ++gridX) {
        auto lawn = authoredTileFromSource(
            gridX, -12, 1, "light_lawn", "auto");
        lawn.reason = "terrain_bench_corner_source_handoff_regression";
        benchCornerLayout.authoredTerrainTiles.push_back(
            std::move(lawn));
    }
    auto benchDirt = authoredTileFromSource(
        19, -11, 1, "dirt_path", "path_10");
    benchDirt.reason = "terrain_bench_corner_source_handoff_regression";
    benchCornerLayout.authoredTerrainTiles.push_back(
        std::move(benchDirt));
    std::erase_if(
        benchCornerLayout.authoredTerrainTiles,
        [](const route1::AuthoredTerrainTile& tile) {
            return tile.gridX >= 22 && tile.gridX <= 24 &&
                (tile.gridZ == -11 || tile.gridZ == -10);
        });
    for (std::int32_t gridX = 22; gridX <= 24; ++gridX) {
        auto lawn = authoredTileFromSource(
            gridX, -10, 1, "light_lawn", "auto");
        lawn.reason = "terrain_wide_path_end_cap_regression";
        benchCornerLayout.authoredTerrainTiles.push_back(
            std::move(lawn));
        auto dirt = authoredTileFromSource(
            gridX,
            -11,
            1,
            "dirt_path",
            gridX == 24 ? "path_8" : "path_10");
        dirt.reason = "terrain_wide_path_end_cap_regression";
        benchCornerLayout.authoredTerrainTiles.push_back(
            std::move(dirt));
    }
    if (!environment.applyBoardLayout(benchCornerLayout, &error) ||
        !environment.setTerrainPatchV2PreviewEnabled(true, &error)) {
        outFail =
            "Terrain Patch V2 rejected the source-preserving bench-corner fixture: " +
            error;
        return false;
    }
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        benchCornerBatches;
    environment.appendIndexedBatches(0.0f, benchCornerBatches);
    std::size_t replacementLawnVertexCount = 0u;
    bool replacementLawnUsesCleanSelector = true;
    constexpr float kCleanLawnUv2U = -0.101646f;
    constexpr float kCleanLawnUv2V = -1.071291f;
    for (const auto& batch : benchCornerBatches) {
        if (batch.geometryCacheKey.find(
                "route1:terrain-authored-surface:") ==
            std::string::npos) {
            continue;
        }
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const auto vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        for (const auto& instance : batch.instances) {
            for (std::size_t vertexIndex = 0u;
                 vertices && vertexIndex < vertexCount;
                 ++vertexIndex) {
                const auto& vertex = vertices[vertexIndex];
                const auto worldPoint = transformPoint(
                    instance.modelMatrix,
                    {vertex.x, vertex.y, vertex.z});
                const auto sourcePoint = transformPoint(
                    sourceFromWorld, worldPoint);
                if (sourcePoint[0] <= 1700.01 ||
                    sourcePoint[0] >= 1899.99 ||
                    sourcePoint[2] <= -1199.99 ||
                    sourcePoint[2] >= -1130.01 ||
                    std::abs(sourcePoint[1] - 50.02) > 0.01) {
                    continue;
                }
                ++replacementLawnVertexCount;
                replacementLawnUsesCleanSelector =
                    replacementLawnUsesCleanSelector &&
                    std::abs(vertex.sourceUv2U - kCleanLawnUv2U) <=
                        0.001f &&
                    std::abs(vertex.sourceUv2V - kCleanLawnUv2V) <=
                        0.001f;
            }
        }
    }
    if (replacementLawnVertexCount == 0u ||
        !replacementLawnUsesCleanSelector) {
        outFail =
            "The Route 1 bench lawn replacement retained an unrelated source dirt/lawn selector field instead of one neutral continuous lawn selector.";
        return false;
    }
    struct LawnSeamField {
        std::array<float, 2> uv0{};
        std::array<float, 2> uv1{};
        std::array<float, 4> color{};
        std::size_t sampleCount = 0u;
    };
    std::map<std::array<std::int64_t, 3>, LawnSeamField>
        westLawnSeamFields;
    float maximumWestLawnUv0Difference = 0.0f;
    float maximumWestLawnUv1Difference = 0.0f;
    float maximumWestLawnColorDifference = 0.0f;
    for (const auto& batch : benchCornerBatches) {
        if (batch.geometryCacheKey.find(
                "route1:terrain-authored-surface:") ==
            std::string::npos) {
            continue;
        }
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const auto vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        for (const auto& instance : batch.instances) {
            for (std::size_t vertexIndex = 0u;
                 vertices && vertexIndex < vertexCount;
                 ++vertexIndex) {
                const auto& vertex = vertices[vertexIndex];
                const auto worldPoint = transformPoint(
                    instance.modelMatrix,
                    {vertex.x, vertex.y, vertex.z});
                const auto sourcePoint = transformPoint(
                    sourceFromWorld, worldPoint);
                if (std::abs(sourcePoint[0] - 1700.0) > 0.1 ||
                    sourcePoint[2] < -1195.1 ||
                    sourcePoint[2] > -1104.9 ||
                    std::abs(sourcePoint[1] - 50.02) > 0.01 ||
                    vertex.sourceUv2V > -1.0f) {
                    continue;
                }
                const std::array<std::int64_t, 3> key{
                    static_cast<std::int64_t>(std::llround(
                        sourcePoint[0] * 10.0)),
                    static_cast<std::int64_t>(std::llround(
                        sourcePoint[1] * 10.0)),
                    static_cast<std::int64_t>(std::llround(
                        sourcePoint[2] * 10.0))};
                auto& field = westLawnSeamFields[key];
                const std::array<float, 2> uv0{vertex.u, vertex.v};
                const std::array<float, 2> uv1{
                    vertex.sourceUv1U,
                    vertex.sourceUv1V};
                const std::array<float, 4> color{
                    vertex.r, vertex.g, vertex.b, vertex.a};
                if (field.sampleCount == 0u) {
                    field.uv0 = uv0;
                    field.uv1 = uv1;
                    field.color = color;
                } else {
                    for (std::size_t channel = 0u; channel < 2u;
                         ++channel) {
                        maximumWestLawnUv0Difference = std::max(
                            maximumWestLawnUv0Difference,
                            std::abs(uv0[channel] - field.uv0[channel]));
                        maximumWestLawnUv1Difference = std::max(
                            maximumWestLawnUv1Difference,
                            std::abs(uv1[channel] - field.uv1[channel]));
                    }
                    for (std::size_t channel = 0u; channel < 4u;
                         ++channel) {
                        maximumWestLawnColorDifference = std::max(
                            maximumWestLawnColorDifference,
                            std::abs(
                                color[channel] - field.color[channel]));
                    }
                }
                ++field.sampleCount;
            }
        }
    }
    const auto pairedWestLawnPositionCount = std::count_if(
        westLawnSeamFields.begin(),
        westLawnSeamFields.end(),
        [](const auto& entry) {
            return entry.second.sampleCount >= 2u;
        });
    if (pairedWestLawnPositionCount < 12u ||
        maximumWestLawnUv0Difference > 0.001f ||
        maximumWestLawnUv1Difference > 0.001f ||
        maximumWestLawnColorDifference > 0.001f) {
        outFail =
            "The generated lawn cells at (16,-12)/(17,-12) did not continue one exact LGPE material field across their shared edge (pairs=" +
            std::to_string(pairedWestLawnPositionCount) +
            ", uv0=" +
            std::to_string(maximumWestLawnUv0Difference) +
            ", uv1=" +
            std::to_string(maximumWestLawnUv1Difference) +
            ", color=" +
            std::to_string(maximumWestLawnColorDifference) + ").";
        return false;
    }
    struct GeneratedBoundarySample {
        std::array<float, 2> uv0{};
        std::array<float, 2> uv2{};
        std::array<float, 4> color{};
    };
    using GeneratedBoundaryKey = std::array<std::int64_t, 3>;
    struct GeneratedBoundaryPair {
        std::vector<GeneratedBoundarySample> lawn;
        std::vector<GeneratedBoundarySample> dirt;
    };
    std::map<GeneratedBoundaryKey, GeneratedBoundaryPair>
        generatedBoundarySamples;
    for (const auto& batch : benchCornerBatches) {
        if (batch.geometryCacheKey.find(
                "route1:terrain-authored-surface:") ==
            std::string::npos) {
            continue;
        }
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const auto vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        for (const auto& instance : batch.instances) {
            for (std::size_t vertexIndex = 0u;
                 vertices && vertexIndex < vertexCount;
                 ++vertexIndex) {
                const auto& vertex = vertices[vertexIndex];
                const auto worldPoint = transformPoint(
                    instance.modelMatrix,
                    {vertex.x, vertex.y, vertex.z});
                const auto sourcePoint = transformPoint(
                    sourceFromWorld, worldPoint);
                if (sourcePoint[0] < 1720.0 ||
                    sourcePoint[0] > 1880.0 ||
                    std::abs(sourcePoint[2] + 1100.0) > 0.1 ||
                    std::abs(sourcePoint[1] - 50.02) > 0.01) {
                    continue;
                }
                const bool lawnBoundary =
                    vertex.sourceUv2V < -1.0f;
                const bool dirtBoundary =
                    vertex.sourceUv2V > 0.9f &&
                    vertex.sourceUv2V < 0.95f;
                if (!lawnBoundary && !dirtBoundary) {
                    continue;
                }
                GeneratedBoundarySample sample{
                    .uv0 = {vertex.u, vertex.v},
                    .uv2 = {
                        vertex.sourceUv2U,
                        vertex.sourceUv2V},
                    .color = {
                        vertex.r,
                        vertex.g,
                        vertex.b,
                        vertex.a}};
                const GeneratedBoundaryKey key{
                    static_cast<std::int64_t>(std::llround(
                        sourcePoint[0] * 10.0)),
                    static_cast<std::int64_t>(std::llround(
                        sourcePoint[1] * 10.0)),
                    static_cast<std::int64_t>(std::llround(
                        sourcePoint[2] * 10.0))};
                auto& pair = generatedBoundarySamples[key];
                (lawnBoundary ? pair.lawn : pair.dirt)
                    .push_back(sample);
            }
        }
    }
    const auto repeatDifference = [](float left, float right) {
        const float difference = left - right;
        return std::abs(difference - std::round(difference));
    };
    std::size_t compatibleBoundaryPairCount = 0u;
    float maximumUv0Difference = 0.0f;
    float maximumUv2RepeatDifference = 0.0f;
    float maximumColorDifference = 0.0f;
    for (const auto& [position, pair] : generatedBoundarySamples) {
        (void)position;
        for (const auto& lawn : pair.lawn) {
            for (const auto& dirt : pair.dirt) {
                maximumUv0Difference = std::max({
                    maximumUv0Difference,
                    std::abs(lawn.uv0[0] - dirt.uv0[0]),
                    std::abs(lawn.uv0[1] - dirt.uv0[1])});
                maximumUv2RepeatDifference = std::max({
                    maximumUv2RepeatDifference,
                    repeatDifference(lawn.uv2[0], dirt.uv2[0]),
                    repeatDifference(lawn.uv2[1], dirt.uv2[1])});
                for (std::size_t channel = 0u;
                     channel < lawn.color.size();
                     ++channel) {
                    maximumColorDifference = std::max(
                        maximumColorDifference,
                        std::abs(
                            lawn.color[channel] -
                            dirt.color[channel]));
                }
                ++compatibleBoundaryPairCount;
            }
        }
    }
    // The material selector must use two vertices at the same position: one
    // carries the lawn-side UV2 row and one carries the dirt-side UV2 row.
    // Requiring one shared vertex here is impossible because an indexed
    // vertex cannot carry both selector values. What must be shared exactly
    // is geometry, UV0 (also sampled at 0.3 scale), Color0, and the repeat-
    // equivalent UV2 phase along the boundary.
    if (compatibleBoundaryPairCount < 8u ||
        maximumUv0Difference > 0.001f ||
        maximumUv2RepeatDifference > 0.001f ||
        maximumColorDifference > 0.001f) {
        outFail =
            "The rebuilt bench dirt/lawn boundary did not preserve coincident geometry and repeat-compatible material fields (pairs=" +
            std::to_string(compatibleBoundaryPairCount) +
            ", uv0-difference=" +
            std::to_string(maximumUv0Difference) +
            ", uv2-repeat-difference=" +
            std::to_string(maximumUv2RepeatDifference) +
            ", color-difference=" +
            std::to_string(maximumColorDifference) + ").";
        return false;
    }
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        preservedBenchCells{{
            {17, -9},
            {18, -9},
            {17, -10},
            {18, -10}}};
    using BenchSourceCounts = std::array<std::size_t, 4>;
    const auto inspectBenchBatches =
        [&](const auto& inspectedBatches,
            BenchSourceCounts& sourceCounts,
            bool& generatedPreservedCell,
            bool& generatedCorner,
            bool& sourceCrossesCanonicalWestEdge,
            bool& retainedCanonicalWestSide,
            bool& foundGeneratedWestEdgeCarrier,
            bool& generatedWestDirtTransitionCarrier,
            bool& generatedWestCleanDirtCarrier) {
            for (const auto& batch : inspectedBatches) {
                const bool generatedSurface =
                    batch.geometryCacheKey.find(
                        "route1:terrain-authored-surface:") !=
                    std::string::npos;
                const auto materialIndex = batch.sharedTemplate
                    ? batch.sharedTemplate->sourceMaterialIndex
                    : batch.sourceMaterialIndex;
                const bool sourceGround = materialIndex == 19u &&
                    batch.geometryCacheKey.starts_with(
                        "published-environment:");
                if (!generatedSurface && !sourceGround) {
                    continue;
                }
                const auto* vertices = batch.sharedVertices
                    ? batch.sharedVertices
                    : batch.vertices.data();
                const auto vertexCount = batch.sharedVertices
                    ? batch.sharedVertexCount
                    : batch.vertices.size();
                const auto* indices = batch.sharedIndices
                    ? batch.sharedIndices
                    : batch.indices.data();
                const auto indexCount = batch.sharedIndices
                    ? batch.sharedIndexCount
                    : batch.indices.size();
                for (const auto& instance : batch.instances) {
                    for (std::size_t index = 0u;
                         vertices && indices && index + 2u < indexCount;
                         index += 3u) {
                        std::array<std::array<double, 3>, 3> points{};
                        bool valid = true;
                        bool sourceTransition = false;
                        for (std::size_t corner = 0u;
                             corner < points.size();
                             ++corner) {
                            const auto vertexIndex = indices[index + corner];
                            if (vertexIndex >= vertexCount) {
                                valid = false;
                                break;
                            }
                            const auto& vertex = vertices[vertexIndex];
                            const auto worldPoint = transformPoint(
                                instance.modelMatrix,
                                {vertex.x, vertex.y, vertex.z});
                            points[corner] = transformPoint(
                                sourceFromWorld, worldPoint);
                            sourceTransition = sourceTransition ||
                                vertex.sourceUv2V > 0.5f;
                            if (generatedSurface &&
                                std::abs(points[corner][0] - 1700.0) <=
                                    2.0 &&
                                points[corner][2] > -1065.0 &&
                                points[corner][2] < -1005.0) {
                                foundGeneratedWestEdgeCarrier = true;
                                generatedWestDirtTransitionCarrier =
                                    generatedWestDirtTransitionCarrier ||
                                    vertex.sourceUv2V > 0.5f;
                                generatedWestCleanDirtCarrier =
                                    generatedWestCleanDirtCarrier ||
                                    (std::abs(
                                         vertex.sourceUv2U +
                                         0.222876f) <= 0.001f &&
                                     std::abs(
                                         vertex.sourceUv2V +
                                         1.127860f) <= 0.001f);
                            }
                        }
                        if (!valid) {
                            continue;
                        }
                        const std::int32_t cellX =
                            static_cast<std::int32_t>(std::floor(
                                (points[0][0] + points[1][0] +
                                 points[2][0]) /
                                300.0));
                        const std::int32_t cellZ =
                            static_cast<std::int32_t>(std::floor(
                                (points[0][2] + points[1][2] +
                                 points[2][2]) /
                                300.0));
                        for (std::size_t target = 0u;
                             target < preservedBenchCells.size();
                             ++target) {
                            if (cellX != preservedBenchCells[target][0] ||
                                cellZ != preservedBenchCells[target][1]) {
                                continue;
                            }
                            if (sourceGround) {
                                ++sourceCounts[target];
                            }
                            generatedPreservedCell =
                                generatedPreservedCell || generatedSurface;
                        }
                        generatedCorner = generatedCorner ||
                            (generatedSurface &&
                             cellX == 17 && cellZ == -11);
                        if (!sourceGround || !sourceTransition) {
                            continue;
                        }
                        double minimumX = points[0][0];
                        double maximumX = points[0][0];
                        double minimumZ = points[0][2];
                        double maximumZ = points[0][2];
                        for (std::size_t corner = 1u;
                             corner < points.size();
                             ++corner) {
                            minimumX = std::min(minimumX, points[corner][0]);
                            maximumX = std::max(maximumX, points[corner][0]);
                            minimumZ = std::min(minimumZ, points[corner][2]);
                            maximumZ = std::max(maximumZ, points[corner][2]);
                        }
                        sourceCrossesCanonicalWestEdge =
                            sourceCrossesCanonicalWestEdge ||
                            (minimumX < 1700.0 && maximumX > 1700.0 &&
                             minimumZ <= -1099.0 &&
                             maximumZ >= -1001.0);
                        retainedCanonicalWestSide =
                            retainedCanonicalWestSide ||
                            (minimumX < 1700.0 &&
                             maximumX <= 1700.001 &&
                             maximumX >= 1699.999 &&
                             minimumZ <= -1099.0 &&
                             maximumZ >= -1001.0);
                    }
                }
            }
        };
    BenchSourceCounts canonicalBenchCounts{};
    BenchSourceCounts editedBenchCounts{};
    bool ignoredGeneratedPreservedCell = false;
    bool ignoredGeneratedCorner = false;
    bool ignoredSourceCrossesCanonicalWestEdge = false;
    bool ignoredRetainedCanonicalWestSide = false;
    bool ignoredGeneratedWestEdgeCarrier = false;
    bool ignoredGeneratedWestDirtTransitionCarrier = false;
    bool ignoredGeneratedWestCleanDirtCarrier = false;
    inspectBenchBatches(
        canonicalBenchReferenceBatches,
        canonicalBenchCounts,
        ignoredGeneratedPreservedCell,
        ignoredGeneratedCorner,
        ignoredSourceCrossesCanonicalWestEdge,
        ignoredRetainedCanonicalWestSide,
        ignoredGeneratedWestEdgeCarrier,
        ignoredGeneratedWestDirtTransitionCarrier,
        ignoredGeneratedWestCleanDirtCarrier);
    bool generatedPreservedCell = false;
    bool generatedCorner = false;
    bool sourceCrossesCanonicalWestEdge = false;
    bool retainedCanonicalWestSide = false;
    bool foundGeneratedWestEdgeCarrier = false;
    bool generatedWestDirtTransitionCarrier = false;
    bool generatedWestCleanDirtCarrier = false;
    inspectBenchBatches(
        benchCornerBatches,
        editedBenchCounts,
        generatedPreservedCell,
        generatedCorner,
        sourceCrossesCanonicalWestEdge,
        retainedCanonicalWestSide,
        foundGeneratedWestEdgeCarrier,
        generatedWestDirtTransitionCarrier,
        generatedWestCleanDirtCarrier);
    std::size_t cleanInternalDirtEdgeVertexCount = 0u;
    bool internalDirtEdgeContainsGrassSelector = false;
    std::size_t continuedWestLawnRibbonVertexCount = 0u;
    std::size_t continuedWestLawnEdgeVertexCount = 0u;
    std::size_t roundedMixedOwnerCornerCandidateCount = 0u;
    std::size_t roundedMixedOwnerCornerVertexCount = 0u;
    double nearestRoundedCornerSampleDistanceSquared =
        std::numeric_limits<double>::max();
    std::array<double, 2> nearestRoundedCornerSample{};
    bool nearestRoundedCornerSampleCleanDirt = true;
    float nearestRoundedCornerSampleUv2V = 0.0f;
    double nearestLawnOwnedTurnSampleDistanceSquared =
        std::numeric_limits<double>::max();
    std::array<double, 2> nearestLawnOwnedTurnSample{};
    float nearestLawnOwnedTurnSampleUv2V = 0.0f;
    std::size_t cornerInnerRibbonVertexCount = 0u;
    std::size_t cornerInnerCleanDirtVertexCount = 0u;
    std::size_t widePathDirtInteriorVertexCount = 0u;
    std::size_t widePathLawnRibbonVertexCount = 0u;
    std::size_t widePathEndCapDirtInteriorVertexCount = 0u;
    std::size_t widePathEndCapLawnRibbonVertexCount = 0u;
    std::size_t roundedCornerBridgeTriangleCount = 0u;
    float maximumRoundedCornerBridgeExtentCm = 0.0f;
    std::size_t cleanWestDirtVertexCount = 0u;
    bool westDirtContainsNonDirtSelector = false;
    float maximumBenchTriangleUv2USpan = 0.0f;
    for (const auto& batch : benchCornerBatches) {
        if (batch.geometryCacheKey.find(
                "route1:terrain-authored-surface:") ==
            std::string::npos) {
            continue;
        }
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices : batch.vertices.data();
        const auto vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount : batch.vertices.size();
        const auto* indices = batch.sharedIndices
            ? batch.sharedIndices : batch.indices.data();
        const auto indexCount = batch.sharedIndices
            ? batch.sharedIndexCount : batch.indices.size();
        for (const auto& instance : batch.instances) {
            for (std::size_t vertexIndex = 0u;
                 vertices && vertexIndex < vertexCount;
                 ++vertexIndex) {
                const auto& vertex = vertices[vertexIndex];
                const auto sourcePoint = transformPoint(
                    sourceFromWorld,
                    transformPoint(
                        instance.modelMatrix,
                        {vertex.x, vertex.y, vertex.z}));
                const bool cleanDirtSelector =
                    repeatDifference(
                        vertex.sourceUv2U, -0.222876f) <= 0.001f &&
                    repeatDifference(
                        vertex.sourceUv2V, -1.127860f) <= 0.001f;
                {
                    const double dx = sourcePoint[0] - 1715.0;
                    const double dz = sourcePoint[2] + 1080.0;
                    const double distanceSquared = dx * dx + dz * dz;
                    if (distanceSquared <
                        nearestRoundedCornerSampleDistanceSquared) {
                        nearestRoundedCornerSampleDistanceSquared =
                            distanceSquared;
                        nearestRoundedCornerSample = {
                            sourcePoint[0], sourcePoint[2]};
                        nearestRoundedCornerSampleCleanDirt =
                            cleanDirtSelector;
                        nearestRoundedCornerSampleUv2V =
                            vertex.sourceUv2V;
                    }
                }
                {
                    const double dx = sourcePoint[0] - 1690.0;
                    const double dz = sourcePoint[2] + 1090.0;
                    const double distanceSquared = dx * dx + dz * dz;
                    if (distanceSquared <
                        nearestLawnOwnedTurnSampleDistanceSquared) {
                        nearestLawnOwnedTurnSampleDistanceSquared =
                            distanceSquared;
                        nearestLawnOwnedTurnSample = {
                            sourcePoint[0], sourcePoint[2]};
                        nearestLawnOwnedTurnSampleUv2V =
                            vertex.sourceUv2V;
                    }
                }
                // The generated horizontal ribbon must remain present all the
                // way to its left endpoint. The adjacent preserved vertical
                // ribbon now performs the turn on its lawn-owned carrier.
                if (sourcePoint[0] > 1708.0 &&
                    sourcePoint[0] < 1725.0 &&
                    sourcePoint[2] > -1090.0 &&
                    sourcePoint[2] < -1070.0) {
                    ++roundedMixedOwnerCornerCandidateCount;
                    if (!cleanDirtSelector) {
                        ++roundedMixedOwnerCornerVertexCount;
                    }
                }
                if (sourcePoint[0] > 1704.0 &&
                    sourcePoint[0] < 1726.0 &&
                    std::abs(sourcePoint[2] + 1070.0) <= 0.2) {
                    if (repeatDifference(
                            vertex.sourceUv2V,
                            0.991155148f) <= 0.002f) {
                        ++cornerInnerRibbonVertexCount;
                    }
                    if (cleanDirtSelector) {
                        ++cornerInnerCleanDirtVertexCount;
                    }
                }
                if (sourcePoint[0] >= 2220.0 &&
                    sourcePoint[0] <= 2280.0 &&
                    sourcePoint[2] >= -1020.0 &&
                    sourcePoint[2] <= -1010.0 &&
                    cleanDirtSelector) {
                    ++widePathDirtInteriorVertexCount;
                }
                if (std::abs(sourcePoint[0] - 2250.0) <= 0.2) {
                    if (std::abs(sourcePoint[2] + 970.0) <= 0.2 &&
                        repeatDifference(
                            vertex.sourceUv2V,
                            0.932880402f) <= 0.003f) {
                        ++widePathLawnRibbonVertexCount;
                    }
                }
                if (sourcePoint[0] >= 2420.0 &&
                    sourcePoint[0] <= 2480.0 &&
                    sourcePoint[2] >= -1020.0 &&
                    sourcePoint[2] <= -1010.0 &&
                    cleanDirtSelector) {
                    ++widePathEndCapDirtInteriorVertexCount;
                }
                if (std::abs(sourcePoint[0] - 2450.0) <= 0.2 &&
                    std::abs(sourcePoint[2] + 970.0) <= 0.2 &&
                    repeatDifference(
                        vertex.sourceUv2V,
                        0.932880402f) <= 0.003f) {
                    ++widePathEndCapLawnRibbonVertexCount;
                }
                if (sourcePoint[2] <= -1065.0 ||
                    sourcePoint[2] >= -1005.0) {
                    continue;
                }
                if (sourcePoint[0] > 1665.0 &&
                    sourcePoint[0] < 1679.0 &&
                    repeatDifference(
                        vertex.sourceUv2V, -1.0690f) <= 0.004f &&
                    repeatDifference(
                        vertex.sourceUv2U, -0.101646f) > 0.001f) {
                    ++continuedWestLawnRibbonVertexCount;
                }
                if (sourcePoint[0] > 1688.0 &&
                    sourcePoint[0] < 1703.0 &&
                    repeatDifference(
                        vertex.sourceUv2V, 0.991155148f) <= 0.002f &&
                    repeatDifference(
                        vertex.sourceUv2U, -0.101646f) > 0.001f) {
                    ++continuedWestLawnEdgeVertexCount;
                }
                if (sourcePoint[0] <= 1706.0 ||
                    sourcePoint[0] >= 1729.0) {
                    continue;
                }
                ++cleanWestDirtVertexCount;
                westDirtContainsNonDirtSelector =
                    westDirtContainsNonDirtSelector ||
                    !cleanDirtSelector;
            }
            for (std::size_t index = 0u;
                 vertices && indices && index + 2u < indexCount;
                 index += 3u) {
                float minimumX = std::numeric_limits<float>::max();
                float maximumX = std::numeric_limits<float>::lowest();
                float minimumZ = std::numeric_limits<float>::max();
                float maximumZ = std::numeric_limits<float>::lowest();
                float minimumU = std::numeric_limits<float>::max();
                float maximumU = std::numeric_limits<float>::lowest();
                bool valid = true;
                for (std::size_t corner = 0u; corner < 3u; ++corner) {
                    const auto vertexIndex = indices[index + corner];
                    if (vertexIndex >= vertexCount) {
                        valid = false;
                        break;
                    }
                    const auto& vertex = vertices[vertexIndex];
                    const auto sourcePoint = transformPoint(
                        sourceFromWorld,
                        transformPoint(
                            instance.modelMatrix,
                            {vertex.x, vertex.y, vertex.z}));
                    minimumX = std::min(
                        minimumX, static_cast<float>(sourcePoint[0]));
                    maximumX = std::max(
                        maximumX, static_cast<float>(sourcePoint[0]));
                    minimumZ = std::min(
                        minimumZ, static_cast<float>(sourcePoint[2]));
                    maximumZ = std::max(
                        maximumZ, static_cast<float>(sourcePoint[2]));
                    minimumU = std::min(minimumU, vertex.sourceUv2U);
                    maximumU = std::max(maximumU, vertex.sourceUv2U);
                    const bool eastWestDirtJoin =
                        std::abs(sourcePoint[0] - 1800.0) <= 0.1 &&
                        sourcePoint[2] > -1065.0 &&
                        sourcePoint[2] < -1005.0;
                    const bool northSouthDirtJoin =
                        std::abs(sourcePoint[2] + 1000.0) <= 0.1 &&
                        sourcePoint[0] > 1735.0 &&
                        sourcePoint[0] < 1795.0;
                    if (eastWestDirtJoin || northSouthDirtJoin) {
                        ++cleanInternalDirtEdgeVertexCount;
                        internalDirtEdgeContainsGrassSelector =
                            internalDirtEdgeContainsGrassSelector ||
                            vertex.sourceUv2V > 0.5f;
                    }
                }
                if (!valid || maximumX < 1700.0f || minimumX > 1800.0f ||
                    maximumZ < -1100.0f || minimumZ > -1000.0f) {
                    continue;
                }
                if (minimumX <= 1715.0f && maximumX >= 1715.0f &&
                    minimumZ <= -1080.0f && maximumZ >= -1080.0f) {
                    ++roundedCornerBridgeTriangleCount;
                    maximumRoundedCornerBridgeExtentCm = std::max(
                        maximumRoundedCornerBridgeExtentCm,
                        std::max(
                            maximumX - minimumX,
                            maximumZ - minimumZ));
                }
                maximumBenchTriangleUv2USpan = std::max(
                    maximumBenchTriangleUv2USpan,
                    maximumU - minimumU);
            }
        }
    }
    // Material-19 source triangles cross logical cells, so rebuilding an
    // adjacent lawn socket can repartition how many triangle centroids land in
    // a preserved cell without replacing that cell. The protected ramp/flat
    // cells remain source-owned, while both halves of the changed dirt/lawn
    // socket are intentionally regenerated as one contour. Retaining only the
    // old lawn half leaves its straight selector card inside the new corner.
    if (std::any_of(
            editedBenchCounts.begin(),
            editedBenchCounts.end(),
            [](std::size_t count) { return count == 0u; }) ||
        generatedPreservedCell || !generatedCorner ||
        sourceCrossesCanonicalWestEdge ||
        !foundGeneratedWestEdgeCarrier ||
        generatedWestDirtTransitionCarrier ||
        !generatedWestCleanDirtCarrier ||
        continuedWestLawnRibbonVertexCount < 8u ||
        continuedWestLawnEdgeVertexCount < 8u ||
        roundedMixedOwnerCornerVertexCount == 0u ||
        repeatDifference(
            nearestRoundedCornerSampleUv2V,
            0.971730233f) > 0.003f ||
        repeatDifference(
            nearestLawnOwnedTurnSampleUv2V,
            0.947719717f) > 0.003f ||
        cornerInnerRibbonVertexCount < 3u ||
        cornerInnerCleanDirtVertexCount < 3u ||
        widePathDirtInteriorVertexCount == 0u ||
        widePathLawnRibbonVertexCount == 0u ||
        widePathEndCapDirtInteriorVertexCount == 0u ||
        widePathEndCapLawnRibbonVertexCount == 0u ||
        cleanWestDirtVertexCount < 8u ||
        westDirtContainsNonDirtSelector ||
        cleanInternalDirtEdgeVertexCount < 8u ||
        internalDirtEdgeContainsGrassSelector ||
        maximumBenchTriangleUv2USpan > 0.75f) {
        outFail =
            "The bench corner did not preserve source-owned ramps/flats while rebuilding both halves of the changed grass socket at cell (17,-11) (reference-source-triangles=" +
            std::to_string(canonicalBenchCounts[0]) + "," +
            std::to_string(canonicalBenchCounts[1]) + "," +
            std::to_string(canonicalBenchCounts[2]) + "," +
            std::to_string(canonicalBenchCounts[3]) +
            "; edited=" +
            std::to_string(editedBenchCounts[0]) + "," +
            std::to_string(editedBenchCounts[1]) + "," +
            std::to_string(editedBenchCounts[2]) + "," +
            std::to_string(editedBenchCounts[3]) +
            "; generated-preserved=" +
            std::to_string(generatedPreservedCell) +
            "; generated-corner=" + std::to_string(generatedCorner) +
            "; source-crosses-west-edge=" +
            std::to_string(sourceCrossesCanonicalWestEdge) +
            "; retained-west-side=" +
            std::to_string(retainedCanonicalWestSide) +
            "; generated-west-carrier=" +
            std::to_string(foundGeneratedWestEdgeCarrier) +
            "; west-dirt-transition-carrier=" +
            std::to_string(generatedWestDirtTransitionCarrier) +
            "; west-clean-dirt-carrier=" +
            std::to_string(generatedWestCleanDirtCarrier) +
            "; west-lawn-ribbon-vertices=" +
            std::to_string(continuedWestLawnRibbonVertexCount) +
            "; west-lawn-edge-vertices=" +
            std::to_string(continuedWestLawnEdgeVertexCount) +
            "; rounded-mixed-owner-corner-vertices=" +
            std::to_string(roundedMixedOwnerCornerVertexCount) +
            "/" +
            std::to_string(roundedMixedOwnerCornerCandidateCount) +
            "; nearest-rounded-corner-sample=" +
            std::to_string(nearestRoundedCornerSample[0]) + "," +
            std::to_string(nearestRoundedCornerSample[1]) + ",clean=" +
            std::to_string(nearestRoundedCornerSampleCleanDirt) +
            ",uv2-v=" +
            std::to_string(nearestRoundedCornerSampleUv2V) +
            "; nearest-lawn-owned-turn-sample=" +
            std::to_string(nearestLawnOwnedTurnSample[0]) + "," +
            std::to_string(nearestLawnOwnedTurnSample[1]) + ",uv2-v=" +
            std::to_string(nearestLawnOwnedTurnSampleUv2V) +
            "; corner-inner-ribbon/clean-vertices=" +
            std::to_string(cornerInnerRibbonVertexCount) + "/" +
            std::to_string(cornerInnerCleanDirtVertexCount) +
            "; wide-path-dirt/lawn=" +
            std::to_string(widePathDirtInteriorVertexCount) + "/" +
            std::to_string(widePathLawnRibbonVertexCount) +
            "; wide-path-end-cap-dirt/lawn=" +
            std::to_string(widePathEndCapDirtInteriorVertexCount) + "/" +
            std::to_string(widePathEndCapLawnRibbonVertexCount) +
            "; rounded-corner-bridge-triangles=" +
            std::to_string(roundedCornerBridgeTriangleCount) +
            ",extent=" +
            std::to_string(maximumRoundedCornerBridgeExtentCm) +
            "; west-clean-dirt-vertices=" +
            std::to_string(cleanWestDirtVertexCount) +
            "; west-dirt-non-dirt-selector=" +
            std::to_string(westDirtContainsNonDirtSelector) +
            "; clean-internal-dirt-edge-vertices=" +
            std::to_string(cleanInternalDirtEdgeVertexCount) +
            "; internal-dirt-edge-grass-selector=" +
            std::to_string(internalDirtEdgeContainsGrassSelector) +
            "; maximum-triangle-uv2-u-span=" +
            std::to_string(maximumBenchTriangleUv2USpan) + ").";
        return false;
    }
    if (!environment.applyBoardLayout(
            cornerContinuationLayout, &error) ||
        !environment.setTerrainPatchV2PreviewEnabled(true, &error)) {
        outFail =
            "The Route 1 corner fixture could not be restored after the material-handoff regression: " +
            error;
        return false;
    }
    auto regionalDonorLayout = environment.layout();
    auto eastSourceSocket = authoredTileFromSource(
        25, -4, 1, "light_lawn", "auto");
    eastSourceSocket.sourceReference =
        std::array<std::int32_t, 2>{22, -4};
    eastSourceSocket.reason = "terrain_tile_paste";
    regionalDonorLayout.authoredTerrainTiles.push_back(
        std::move(eastSourceSocket));
    if (!environment.applyBoardLayout(regionalDonorLayout, &error)) {
        outFail =
            "The exact-donor corner socket regression fixture was rejected: " +
            error;
        return false;
    }
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        donorSocketBatches;
    environment.appendIndexedBatches(0.0f, donorSocketBatches);
    using DonorVertexKey = std::array<std::int64_t, 3>;
    using DonorTriangleKey = std::tuple<
        std::uint32_t,
        DonorVertexKey,
        DonorVertexKey,
        DonorVertexKey>;
    std::set<DonorTriangleKey> donorTriangles;
    bool foundDonorTriangle = false;
    bool duplicateDonorTriangle = false;
    for (const auto& batch : donorSocketBatches) {
        if (batch.geometryCacheKey.find(
                "terrain-source-reference-patch:") ==
            std::string::npos) {
            continue;
        }
        const auto* vertices = batch.sharedVertices
            ? batch.sharedVertices
            : batch.vertices.data();
        const auto vertexCount = batch.sharedVertices
            ? batch.sharedVertexCount
            : batch.vertices.size();
        const auto* indices = batch.sharedIndices
            ? batch.sharedIndices
            : batch.indices.data();
        const auto indexCount = batch.sharedIndices
            ? batch.sharedIndexCount
            : batch.indices.size();
        const auto materialIndex = batch.sharedTemplate
            ? batch.sharedTemplate->sourceMaterialIndex
            : batch.sourceMaterialIndex;
        for (const auto& instance : batch.instances) {
            for (std::size_t index = 0u;
                 vertices && indices && index + 2u < indexCount;
                 index += 3u) {
                std::array<DonorVertexKey, 3> triangle{};
                bool valid = true;
                for (std::size_t corner = 0u;
                     corner < triangle.size();
                     ++corner) {
                    const auto vertexIndex = indices[index + corner];
                    if (vertexIndex >= vertexCount) {
                        valid = false;
                        break;
                    }
                    const auto& vertex = vertices[vertexIndex];
                    const auto position = transformPoint(
                        instance.modelMatrix,
                        {vertex.x, vertex.y, vertex.z});
                    for (std::size_t axis = 0u;
                         axis < 3u;
                         ++axis) {
                        triangle[corner][axis] =
                            static_cast<std::int64_t>(std::llround(
                                position[axis] * 1000.0));
                    }
                }
                if (!valid) {
                    continue;
                }
                foundDonorTriangle = true;
                std::sort(triangle.begin(), triangle.end());
                if (!donorTriangles.emplace(
                        materialIndex,
                        triangle[0],
                        triangle[1],
                        triangle[2]).second) {
                    duplicateDonorTriangle = true;
                }
            }
        }
    }
    if (!foundDonorTriangle || duplicateDonorTriangle) {
        outFail =
            "A Route 1 exact-donor socket did not submit each canonical material triangle exactly once (found=" +
            std::to_string(foundDonorTriangle) +
            ", duplicate=" +
            std::to_string(duplicateDonorTriangle) + ").";
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
                workspace,
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
        const auto& variantStats = variantEnvironment.stats();
        if (variantStats.shadowTriangleCount == 0u ||
            (variant == &variants::kRoute1 &&
             variantStats.shadowGroundTriangleCount == 0u) ||
            (variant == &variants::kRoute1_5 &&
             variantStats.shadowGroundTriangleCount != 0u)) {
            outFail =
                "Route 1 scene variant did not preserve its projected-shadow caster contract (scene=" +
                std::string(variant->sceneId) +
                ", all=" +
                std::to_string(variantStats.shadowTriangleCount) +
                ", ground=" +
                std::to_string(
                    variantStats.shadowGroundTriangleCount) +
                ").";
            return false;
        }
        if (variant == &variants::kRoute1_5) {
            if (!variantEnvironment.setTerrainPatchV2PreviewEnabled(
                    true, &error)) {
                outFail =
                    "South Clearing could not enable its Terrain Patch V2 seam contract: " +
                    error;
                return false;
            }
            struct LawnBoundaryField {
                float y = 0.0f;
                std::array<float, 2> uv0{};
                std::array<float, 2> uv1{};
                std::array<float, 4> color{};
                std::array<float, 3> normal{};
                std::size_t sampleCount = 0u;
            };
            std::map<std::array<std::int64_t, 2>, LawnBoundaryField>
                boundaryFields;
            float maximumBoundaryHeightDifference = 0.0f;
            float maximumBoundaryUv0Difference = 0.0f;
            float maximumBoundaryUv1Difference = 0.0f;
            float maximumBoundaryColorDifference = 0.0f;
            float maximumBoundaryNormalDifference = 0.0f;
            float maximumRetiredSourceDirtColorDifference = 0.0f;
            std::size_t retiredSourceDirtTintBandSampleCount = 0u;
            const auto cleanFlatDirtColor =
                route1::route1CleanFlatDirtColor();
            std::vector<
                game::runtime::shared_world_batches::WorldIndexedBatch>
                variantBatches;
            variantEnvironment.appendIndexedBatches(
                0.0f, variantBatches);
            const auto variantSourceFromWorld =
                route1::sourceFromWorldMatrix(
                    variantEnvironment.layout());
            constexpr std::array<std::array<double, 2>, 3>
                transplantedSurfaceProbes{{
                    {1650.0, -1250.0},
                    {1650.0, -1350.0},
                    {1650.0, -1450.0},
                }};
            std::array<std::size_t, 3>
                donorSurfaceProbeCounts{};
            std::array<std::size_t, 3>
                canonicalSurfaceProbeCounts{};
            for (const auto& batch : variantBatches) {
                const auto materialIndex = batch.sharedTemplate
                    ? batch.sharedTemplate->sourceMaterialIndex
                    : batch.sourceMaterialIndex;
                if (materialIndex != 19u) {
                    continue;
                }
                const bool donorSurface =
                    batch.geometryCacheKey.find(
                        "terrain-source-reference-patch:") !=
                    std::string::npos;
                const bool canonicalSurface =
                    !donorSurface &&
                    batch.geometryCacheKey.find(":terrain-mask:") !=
                        std::string::npos;
                if (!donorSurface && !canonicalSurface) {
                    continue;
                }
                const auto* vertices = batch.sharedVertices
                    ? batch.sharedVertices
                    : batch.vertices.data();
                const auto vertexCount = batch.sharedVertices
                    ? batch.sharedVertexCount
                    : batch.vertices.size();
                const auto* indices = batch.sharedIndices
                    ? batch.sharedIndices
                    : batch.indices.data();
                const auto indexCount = batch.sharedIndices
                    ? batch.sharedIndexCount
                    : batch.indices.size();
                for (const auto& instance : batch.instances) {
                    for (std::size_t index = 0u;
                         vertices && indices &&
                         index + 2u < indexCount;
                         index += 3u) {
                        std::array<std::array<double, 3>, 3>
                            triangle{};
                        bool valid = true;
                        for (std::size_t corner = 0u;
                             corner < triangle.size();
                             ++corner) {
                            const auto vertexIndex =
                                indices[index + corner];
                            if (vertexIndex >= vertexCount) {
                                valid = false;
                                break;
                            }
                            const auto& vertex = vertices[vertexIndex];
                            triangle[corner] = transformPoint(
                                variantSourceFromWorld,
                                transformPoint(
                                    instance.modelMatrix,
                                    {vertex.x, vertex.y, vertex.z}));
                        }
                        if (!valid) {
                            continue;
                        }
                        for (std::size_t probe = 0u;
                             probe < transplantedSurfaceProbes.size();
                             ++probe) {
                            if (!containsXZ(
                                    triangle,
                                    transplantedSurfaceProbes[probe][0],
                                    transplantedSurfaceProbes[probe][1])) {
                                continue;
                            }
                            if (donorSurface) {
                                ++donorSurfaceProbeCounts[probe];
                            } else {
                                ++canonicalSurfaceProbeCounts[probe];
                            }
                        }
                    }
                }
            }
            const auto periodicDifference = [](float left,
                                               float right) {
                const float difference = left - right;
                return std::abs(
                    difference - std::round(difference));
            };
            for (const auto& batch : variantBatches) {
                if (batch.geometryCacheKey.find(
                        "route1:terrain-authored-surface:") ==
                    std::string::npos) {
                    continue;
                }
                const auto* vertices = batch.sharedVertices
                    ? batch.sharedVertices
                    : batch.vertices.data();
                const auto vertexCount = batch.sharedVertices
                    ? batch.sharedVertexCount
                    : batch.vertices.size();
                for (const auto& instance : batch.instances) {
                    for (std::size_t vertexIndex = 0u;
                         vertices && vertexIndex < vertexCount;
                         ++vertexIndex) {
                        const auto& vertex = vertices[vertexIndex];
                        const auto worldPoint = transformPoint(
                            instance.modelMatrix,
                            {vertex.x, vertex.y, vertex.z});
                        const auto sourcePoint = transformPoint(
                            variantSourceFromWorld, worldPoint);
                        // The active path connects across these former
                        // source dirt/lawn boundaries. Its interior band must
                        // remain neutral dirt instead of retaining the old
                        // grass-side Color0 tint.
                        const bool retiredSourceDirtTintBand =
                            (sourcePoint[0] >= 1874.9 &&
                             sourcePoint[0] <= 1895.1 &&
                             sourcePoint[2] >= -1645.1 &&
                             sourcePoint[2] <= -1554.9) ||
                            (sourcePoint[2] >= -1525.1 &&
                             sourcePoint[2] <= -1504.9 &&
                             sourcePoint[0] >= 1754.9 &&
                             sourcePoint[0] <= 1845.1);
                        if (retiredSourceDirtTintBand) {
                            const std::array<float, 4> color{
                                vertex.r, vertex.g,
                                vertex.b, vertex.a};
                            for (std::size_t channel = 0u;
                                 channel < color.size();
                                 ++channel) {
                                maximumRetiredSourceDirtColorDifference =
                                    std::max(
                                        maximumRetiredSourceDirtColorDifference,
                                        std::abs(
                                            color[channel] -
                                            cleanFlatDirtColor[channel]));
                            }
                            ++retiredSourceDirtTintBandSampleCount;
                        }
                        if (std::abs(sourcePoint[0] - 2500.0) > 0.1 ||
                            sourcePoint[2] <= -1695.1 ||
                            sourcePoint[2] >= -1604.9) {
                            continue;
                        }
                        const std::array<std::int64_t, 2> key{
                            static_cast<std::int64_t>(std::llround(
                                sourcePoint[0] * 10.0)),
                            static_cast<std::int64_t>(std::llround(
                                sourcePoint[2] * 10.0))};
                        auto& field = boundaryFields[key];
                        const std::array<float, 2> uv0{
                            vertex.u, vertex.v};
                        const std::array<float, 2> uv1{
                            vertex.sourceUv1U,
                            vertex.sourceUv1V};
                        const std::array<float, 4> color{
                            vertex.r, vertex.g,
                            vertex.b, vertex.a};
                        const std::array<float, 3> normal{
                            vertex.nx, vertex.ny, vertex.nz};
                        if (field.sampleCount == 0u) {
                            field.y = static_cast<float>(sourcePoint[1]);
                            field.uv0 = uv0;
                            field.uv1 = uv1;
                            field.color = color;
                            field.normal = normal;
                        } else {
                            maximumBoundaryHeightDifference = std::max(
                                maximumBoundaryHeightDifference,
                                std::abs(
                                    static_cast<float>(sourcePoint[1]) -
                                    field.y));
                            for (std::size_t channel = 0u;
                                 channel < 2u;
                                 ++channel) {
                                maximumBoundaryUv0Difference = std::max(
                                    maximumBoundaryUv0Difference,
                                    periodicDifference(
                                        uv0[channel],
                                        field.uv0[channel]));
                                maximumBoundaryUv1Difference = std::max(
                                    maximumBoundaryUv1Difference,
                                    periodicDifference(
                                        uv1[channel],
                                        field.uv1[channel]));
                            }
                            for (std::size_t channel = 0u;
                                 channel < 4u;
                                 ++channel) {
                                maximumBoundaryColorDifference = std::max(
                                    maximumBoundaryColorDifference,
                                    std::abs(
                                        color[channel] -
                                        field.color[channel]));
                            }
                            for (std::size_t channel = 0u;
                                 channel < 3u;
                                 ++channel) {
                                maximumBoundaryNormalDifference = std::max(
                                    maximumBoundaryNormalDifference,
                                    std::abs(
                                        normal[channel] -
                                        field.normal[channel]));
                            }
                        }
                        ++field.sampleCount;
                    }
                }
            }
            const auto pairedPositionCount = std::count_if(
                boundaryFields.begin(),
                boundaryFields.end(),
                [](const auto& entry) {
                    return entry.second.sampleCount >= 2u;
                });
            const bool missingDonorSurface = std::any_of(
                donorSurfaceProbeCounts.begin(),
                donorSurfaceProbeCounts.end(),
                [](std::size_t count) { return count == 0u; });
            const bool retainedCanonicalSurface = std::any_of(
                canonicalSurfaceProbeCounts.begin(),
                canonicalSurfaceProbeCounts.end(),
                [](std::size_t count) { return count != 0u; });
            if (missingDonorSurface || retainedCanonicalSurface ||
                pairedPositionCount < 8u ||
                retiredSourceDirtTintBandSampleCount < 16u ||
                maximumRetiredSourceDirtColorDifference > 0.001f ||
                maximumBoundaryHeightDifference > 0.001f ||
                maximumBoundaryUv0Difference > 0.001f ||
                maximumBoundaryUv1Difference > 0.001f ||
                maximumBoundaryColorDifference > 0.001f ||
                maximumBoundaryNormalDifference > 0.001f) {
                outFail =
                    "South Clearing did not reconcile its generated lawn and retired source dirt/lawn boundaries as continuous material fields (lawn-pairs=" +
                    std::to_string(pairedPositionCount) +
                    ", donor-surface-probes=" +
                    std::to_string(donorSurfaceProbeCounts[0]) + "," +
                    std::to_string(donorSurfaceProbeCounts[1]) + "," +
                    std::to_string(donorSurfaceProbeCounts[2]) +
                    ", canonical-surface-probes=" +
                    std::to_string(canonicalSurfaceProbeCounts[0]) + "," +
                    std::to_string(canonicalSurfaceProbeCounts[1]) + "," +
                    std::to_string(canonicalSurfaceProbeCounts[2]) +
                    ", retired-dirt-tint-samples=" +
                    std::to_string(
                        retiredSourceDirtTintBandSampleCount) +
                    ", retired-dirt-color=" +
                    std::to_string(
                        maximumRetiredSourceDirtColorDifference) +
                    ", height=" +
                    std::to_string(maximumBoundaryHeightDifference) +
                    ", uv0=" +
                    std::to_string(maximumBoundaryUv0Difference) +
                    ", uv1=" +
                    std::to_string(maximumBoundaryUv1Difference) +
                    ", color=" +
                    std::to_string(maximumBoundaryColorDifference) +
                    ", normal=" +
                    std::to_string(maximumBoundaryNormalDifference) + ").";
                return false;
            }
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
