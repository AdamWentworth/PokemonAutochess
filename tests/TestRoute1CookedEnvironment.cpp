#include "engine/core/IAssetStore.h"
#include "engine/core/Paths.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

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
        !normalizedTile->cleanSuppressedEncounterGrassTint) {
        outFail =
            "Authored source-tint normalization did not activate the clean lawn Color0 path.";
        return false;
    }
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>
        batches;
    environment.appendIndexedBatches(0.0f, batches);
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
        const bool sourceBaseline =
            variant == &variants::kRoute1;
        if (variantScene.sceneId != variant->sceneId ||
            variantEnvironment.layout().terrainGridOrigin !=
                expectedOrigin ||
            (sourceBaseline && !variantScene.nodes.empty()) ||
            (!sourceBaseline && variantScene.nodes.empty())) {
            outFail =
                "Route 1 scene variant lost its independent identity, board "
                "registration, source-baseline state, or authored layout: " +
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
