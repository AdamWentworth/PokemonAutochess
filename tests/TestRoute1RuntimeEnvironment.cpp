#include "engine/core/IAssetStore.h"
#include "game/render/environment/Route1FieldSmallGrassMaterial.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"
#include "game/runtime/shared/scene/Route1TerrainAssemblies.h"
#include "game/runtime/shared/scene/Route1TerrainLedgeResolver.h"
#include "game/runtime/shared/scene/Route1TerrainSeamResolver.h"
#include "game/runtime/shared/scene/Route1TreeInstances.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

namespace tree_instances =
    game::runtime::route1_tree_instances;

class MemoryAssetStore final : public engine::IAssetStore {
public:
    bool readText(
        const std::string& virtualPath,
        std::string& outText,
        std::string* outError) const override {
        const auto found = texts.find(virtualPath);
        if (found == texts.end()) {
            if (outError) *outError = "missing text";
            return false;
        }
        outText = found->second;
        return true;
    }

    bool readBytes(
        const std::string&,
        std::vector<std::uint8_t>&,
        std::string* outError) const override {
        if (outError) *outError = "missing bytes";
        return false;
    }

    bool exists(const std::string& virtualPath) const override {
        return texts.contains(virtualPath);
    }

    std::unordered_map<std::string, std::string> texts;
};

std::array<float, 4> transformPoint(
    const std::array<float, 16>& matrix,
    const std::array<float, 4>& point) {
    std::array<float, 4> out{};
    for (std::size_t row = 0u; row < 4u; ++row) {
        for (std::size_t column = 0u; column < 4u; ++column) {
            out[row] +=
                matrix[column * 4u + row] * point[column];
        }
    }
    return out;
}

bool close(float lhs, float rhs) {
    return std::abs(lhs - rhs) <= 0.002f;
}

float dot(
    const std::array<float, 4>& lhs,
    const std::array<float, 4>& rhs) {
    float out = 0.0f;
    for (std::size_t index = 0u; index < lhs.size(); ++index) {
        out += lhs[index] * rhs[index];
    }
    return out;
}

} // namespace

bool test_route1_runtime_environment_contract(std::string& outFail) {
    namespace variants =
        game::runtime::route1_scene_variants;
    if (!variants::editable("routes/route1") ||
        !variants::editable("routes/route1-5") ||
        variants::editable("routes/route2") ||
        variants::fromStateScriptPath(
            "scripts/states/route1_5.lua").sceneId !=
            variants::kRoute1_5.sceneId ||
        variants::fromStateScriptPath(
            "scripts/states/route1.lua").sceneId !=
            variants::kRoute1.sceneId) {
        outFail =
            "Route 1 scene-variant routing no longer distinguishes the entry and follow-up encounters.";
        return false;
    }
    MemoryAssetStore store;
    store.texts["layout.json"] = R"json(
{
  "schema_version": 1,
  "kind": "route1_environment_board_layout",
  "coordinate_system": "source_centimetres_xyz_y_up",
  "source_profile_id": "route1_environment_road001_00",
  "source_to_world": {
    "source_units_to_world": 0.01,
    "source_anchor_cm": [2200.0, 0.0, -1700.0],
    "world_anchor": [0.0, -0.04, 0.0],
    "yaw_degrees": 0.0
  },
  "local_layout_deltas": []
}
)json";

    using namespace game::runtime::route1_environment;
    {
        const auto tile = [](
                              std::int32_t gridX,
                              std::int32_t gridZ,
                              bool authored = true) {
            TerrainTileState value;
            value.gridX = gridX;
            value.gridZ = gridZ;
            value.sourceElevationLevel = 0;
            value.elevationLevel = 0;
            value.sourceSurface = "light_lawn";
            value.sourceShape = "flat";
            value.surface = "light_lawn";
            value.shape = "flat";
            value.sourceOccupied = true;
            value.authored = authored;
            return value;
        };

        std::vector<TerrainTileState> seamTiles;
        auto changed = tile(0, 0);
        changed.sourceElevationLevel = 1;
        seamTiles.push_back(changed);
        seamTiles.push_back(tile(1, 0));
        seamTiles.push_back(tile(2, 0));
        seamTiles.push_back(tile(3, 0, false));

        auto pathBoundary = tile(0, 1);
        pathBoundary.sourceSurface = "dirt_path";
        pathBoundary.surface = "dirt_path";
        seamTiles.push_back(pathBoundary);

        auto incompatibleRamp = tile(-1, 0);
        incompatibleRamp.sourceShape = "ramp_east";
        incompatibleRamp.shape = "ramp_east";
        seamTiles.push_back(incompatibleRamp);

        seamTiles.push_back(tile(10, 0));

        auto shadowReceiver = tile(20, 0);
        auto shadowlessNeighbor = tile(21, 0);
        shadowlessNeighbor.receivesProjectedShadow = false;
        seamTiles.push_back(shadowReceiver);
        seamTiles.push_back(shadowlessNeighbor);

        auto differentSurfaceReceiver = tile(30, 0);
        auto differentSurfaceShadowless = tile(31, 0);
        differentSurfaceShadowless.sourceSurface = "dirt_path";
        differentSurfaceShadowless.surface = "dirt_path";
        differentSurfaceShadowless.receivesProjectedShadow = false;
        seamTiles.push_back(differentSurfaceReceiver);
        seamTiles.push_back(differentSurfaceShadowless);

        auto differentHeightReceiver = tile(40, 0);
        auto differentHeightShadowless = tile(41, 0);
        differentHeightShadowless.sourceElevationLevel = 1;
        differentHeightShadowless.elevationLevel = 1;
        differentHeightShadowless.receivesProjectedShadow = false;
        seamTiles.push_back(differentHeightReceiver);
        seamTiles.push_back(differentHeightShadowless);

        const auto resolution =
            game::runtime::route1_terrain_seams::resolve(
                seamTiles);
        if (resolution.continuousFieldCellCount != 3u ||
            !seamTiles[0].rebuildContinuousMaterialFields ||
            !seamTiles[1].rebuildContinuousMaterialFields ||
            !seamTiles[2].rebuildContinuousMaterialFields ||
            seamTiles[3].rebuildContinuousMaterialFields ||
            seamTiles[4].rebuildContinuousMaterialFields ||
            seamTiles[5].rebuildContinuousMaterialFields ||
            seamTiles[6].rebuildContinuousMaterialFields) {
            outFail =
                "Route 1 seam resolution must propagate a continuous material field only through compatible authored neighbors and stop at untouched source, surface changes, and height-profile changes.";
            return false;
        }
        if (resolution.projectedShadowMismatchEdgeCount != 1u ||
            seamTiles[7].projectedShadowMismatchEdgeMask !=
                static_cast<std::uint8_t>(1u << 1u) ||
            seamTiles[8].projectedShadowMismatchEdgeMask !=
                static_cast<std::uint8_t>(1u << 3u) ||
            seamTiles[9].projectedShadowMismatchEdgeMask != 0u ||
            seamTiles[10].projectedShadowMismatchEdgeMask != 0u ||
            seamTiles[11].projectedShadowMismatchEdgeMask != 0u ||
            seamTiles[12].projectedShadowMismatchEdgeMask != 0u) {
            outFail =
                "Route 1 seam diagnostics must report each compatible projected-shadow discontinuity once without warning across surface or elevation boundaries.";
            return false;
        }
    }
    {
        const auto tile = [](
                              std::int32_t gridX,
                              std::int32_t gridZ,
                              std::int32_t elevationLevel,
                              bool authored) {
            TerrainTileState value;
            value.gridX = gridX;
            value.gridZ = gridZ;
            value.sourceElevationLevel = 1;
            value.elevationLevel = elevationLevel;
            value.sourceSurface = "light_lawn";
            value.sourceShape = "flat";
            value.surface = "light_lawn";
            value.shape = "flat";
            value.sourceOccupied = true;
            value.authored = authored;
            return value;
        };
        std::vector<TerrainTileState> sourceTiles;
        std::vector<TerrainTileState> currentTiles;
        for (std::int32_t gridZ = -4; gridZ <= -1; ++gridZ) {
            sourceTiles.push_back(tile(24, gridZ, 1, false));
            sourceTiles.push_back(tile(25, gridZ, 1, false));
            currentTiles.push_back(tile(24, gridZ, 0, true));
            currentTiles.push_back(tile(25, gridZ, 1, false));
        }
        auto emptyPadding = tile(24, 0, 0, false);
        emptyPadding.sourceOccupied = false;
        sourceTiles.push_back(emptyPadding);
        currentTiles.push_back(emptyPadding);
        emptyPadding.gridX = 25;
        sourceTiles.push_back(emptyPadding);
        currentTiles.push_back(emptyPadding);
        const auto ledges =
            game::runtime::route1_terrain_ledges::resolve(
                currentTiles,
                sourceTiles,
                {});
        std::vector<float> contourStarts;
        std::vector<float> materialContourStarts;
        std::size_t straightJoinEndpointCount = 0u;
        std::size_t openJoinEndpointCount = 0u;
        std::size_t convexJoinEndpointCount = 0u;
        for (const auto& ledge : ledges.edges) {
            if (ledge.ownerCell.first == 25 &&
                ledge.edge == 3u) {
                contourStarts.push_back(ledge.contourStartCm);
                materialContourStarts.push_back(
                    ledge.materialContourStartCm);
                straightJoinEndpointCount +=
                    ledge.startJoin ==
                        game::runtime::route1_terrain_ledges::
                            Join::Straight;
                straightJoinEndpointCount +=
                    ledge.endJoin ==
                        game::runtime::route1_terrain_ledges::
                            Join::Straight;
                openJoinEndpointCount +=
                    ledge.startJoin ==
                        game::runtime::route1_terrain_ledges::
                            Join::Open;
                openJoinEndpointCount +=
                    ledge.endJoin ==
                        game::runtime::route1_terrain_ledges::
                            Join::Open;
                convexJoinEndpointCount +=
                    ledge.startJoin ==
                        game::runtime::route1_terrain_ledges::
                            Join::Convex;
                convexJoinEndpointCount +=
                    ledge.endJoin ==
                        game::runtime::route1_terrain_ledges::
                            Join::Convex;
                if (ledge.profile.tileLevels !=
                        std::array<std::int32_t, 2>{1, 1} ||
                    ledge.profile.neighborLevels !=
                        std::array<std::int32_t, 2>{0, 0}) {
                    outFail =
                        "A lowered Route 1 strip lost the exact endpoint levels of its rebuilt side ledge.";
                    return false;
                }
            }
        }
        std::sort(contourStarts.begin(), contourStarts.end());
        std::sort(
            materialContourStarts.begin(),
            materialContourStarts.end());
        const bool consecutiveContourStarts =
            contourStarts.size() == 4u &&
            close(contourStarts[1] - contourStarts[0], 100.0f) &&
            close(contourStarts[2] - contourStarts[1], 100.0f) &&
            close(contourStarts[3] - contourStarts[2], 100.0f);
        const bool consecutiveMaterialContourStarts =
            materialContourStarts.size() == 4u &&
            close(
                materialContourStarts[1] -
                    materialContourStarts[0],
                100.0f) &&
            close(
                materialContourStarts[2] -
                    materialContourStarts[1],
                100.0f) &&
            close(
                materialContourStarts[3] -
                    materialContourStarts[2],
                100.0f);
        if (ledges.edges.size() != 4u ||
            ledges.contourCount != 1u ||
            !consecutiveContourStarts ||
            !consecutiveMaterialContourStarts ||
            straightJoinEndpointCount != 6u ||
            convexJoinEndpointCount != 0u ||
            openJoinEndpointCount != 2u) {
            const auto distances = [](const std::vector<float>& values) {
                std::string result;
                for (const float value : values) {
                    if (!result.empty()) {
                        result += ",";
                    }
                    result += std::to_string(value);
                }
                return result;
            };
            outFail =
                "Four adjacent rebuilt Route 1 edges must remain one consecutive run with straight internal joins and full-width open endpoints at the source location boundary (logical=" +
                distances(contourStarts) +
                ", material=" +
                distances(materialContourStarts) + ").";
            return false;
        }
        const auto* terminalSide =
            game::runtime::route1_terrain_ledges::find(
                ledges, {25, -1}, 3u);
        game::runtime::route1_terrain_ledges::RebuiltEdge
            convexIncoming;
        game::runtime::route1_terrain_ledges::RebuiltEdge
            convexOutgoing;
        convexIncoming.contourIndex = 7u;
        convexOutgoing.contourIndex = 7u;
        convexIncoming.endJoin =
            game::runtime::route1_terrain_ledges::Join::Convex;
        convexOutgoing.startJoin =
            game::runtime::route1_terrain_ledges::Join::Convex;
        if (!terminalSide ||
            terminalSide->endJoin !=
                game::runtime::route1_terrain_ledges::Join::Open ||
            !close(
                game::runtime::route1_terrain_ledges::endpointAlongCm(
                    terminalSide->endJoin,
                    false,
                    game::runtime::route1_terrain_ledges::
                        kConvexCornerRadiusCm),
                50.0f) ||
            game::runtime::route1_terrain_ledges::formsConvexCorner(
                nullptr, terminalSide) ||
            !game::runtime::route1_terrain_ledges::formsConvexCorner(
                &convexIncoming, &convexOutgoing)) {
            outFail =
                "Route 1 must emit rounded corner carriers only for a fully resolved convex handoff, never beside an open source-domain endpoint.";
            return false;
        }

        const std::vector<TerrainTileState> joinedSourceTiles{
            tile(0, 0, 1, false),
            tile(0, 1, 1, false),
            tile(1, 0, 0, false),
            tile(1, 1, 0, false)};
        const std::vector<TerrainTileState> joinedCurrentTiles{
            tile(0, 0, 1, false),
            tile(0, 1, 0, true),
            tile(1, 0, 0, false),
            tile(1, 1, 0, false)};
        const auto joinedLedges =
            game::runtime::route1_terrain_ledges::resolve(
                joinedCurrentTiles,
                joinedSourceTiles,
                {});
        const auto* changedEdge =
            game::runtime::route1_terrain_ledges::find(
                joinedLedges, {0, 0}, 0u);
        const auto* sourceContinuation =
            game::runtime::route1_terrain_ledges::find(
                joinedLedges, {0, 0}, 1u);
        if (!changedEdge || !sourceContinuation ||
            changedEdge->rebuildsJoinedSourceBoundary ||
            !sourceContinuation->rebuildsJoinedSourceBoundary ||
            !game::runtime::route1_terrain_ledges::formsConvexCorner(
                changedEdge, sourceContinuation)) {
            outFail =
                "A rebuilt Route 1 boundary must carry one adjoining source edge through a convex handoff so the rounded corner cannot lead into a clipped source ledge.";
            return false;
        }
    }
    {
        const auto tile = [](
                              std::int32_t gridX,
                              std::int32_t gridZ,
                              std::int32_t elevationLevel,
                              bool authored) {
            TerrainTileState value;
            value.gridX = gridX;
            value.gridZ = gridZ;
            value.sourceElevationLevel = 1;
            value.elevationLevel = elevationLevel;
            value.sourceSurface = "light_lawn";
            value.sourceShape = "flat";
            value.surface = "light_lawn";
            value.shape = "flat";
            value.sourceOccupied = true;
            value.authored = authored;
            return value;
        };
        const std::vector<TerrainTileState> sourceTiles{
            tile(0, 0, 1, false),
            tile(1, 0, 1, false),
            tile(0, 1, 1, false),
            tile(1, 1, 1, false)};
        const std::vector<TerrainTileState> currentTiles{
            tile(0, 0, 1, false),
            tile(1, 0, 1, false),
            tile(0, 1, 0, true),
            tile(1, 1, 1, false)};
        const auto ledges =
            game::runtime::route1_terrain_ledges::resolve(
                currentTiles,
                sourceTiles,
                {});
        const auto* northEdge =
            game::runtime::route1_terrain_ledges::find(
                ledges, {0, 0}, 0u);
        const auto* westEdge =
            game::runtime::route1_terrain_ledges::find(
                ledges, {1, 1}, 3u);
        using game::runtime::route1_terrain_ledges::Join;
        if (!northEdge || !westEdge ||
            northEdge->endJoin != Join::Concave ||
            westEdge->startJoin != Join::Concave ||
            !close(
                game::runtime::route1_terrain_ledges::
                    endpointAlongCm(Join::Concave, false, 25.0f),
                25.0f) ||
            !close(
                game::runtime::route1_terrain_ledges::
                    endpointAlongCm(Join::Concave, true, 25.0f),
                -25.0f) ||
            !close(
                game::runtime::route1_terrain_ledges::
                    endpointAlongCm(Join::Convex, false, 25.0f),
                50.0f -
                    game::runtime::route1_terrain_ledges::
                        kConvexCornerRadiusCm)) {
            outFail =
                "A concave Route 1 ledge turn must recede each carrier row by its outward distance while a convex turn reserves the shared rounded-corner footprint.";
            return false;
        }
    }
    {
        const BoardLayoutTransform promotedLayout;
        if (promotedLayout.terrainGridOrigin !=
                std::array<std::int32_t, 2>{17, -19} ||
            promotedLayout.benchGapCells != 0u ||
            northBenchTerrainGridOrigin(promotedLayout) !=
                std::array<std::int32_t, 2>{17, -11} ||
            southBenchTerrainGridOrigin(promotedLayout) !=
                std::array<std::int32_t, 2>{17, -20}) {
            outFail =
                "The cooked Route 1 fallback registration must retain the pinned north-clearing footprint and adjacent bench rows.";
            return false;
        }
    }
    {
        const TerrainTileState platformCorner{
            .gridX = 16,
            .gridZ = -13,
            .elevationLevel = 2,
            .surface = "light_lawn",
            .shape = "flat",
            .sourceOccupied = true};
        const TerrainTileState boardRamp{
            .gridX = 17,
            .gridZ = -13,
            .elevationLevel = 1,
            .surface = "dirt_path",
            .shape = "ramp_south",
            .authored = true};
        const auto eastProfile = route1TerrainSharedEdgeProfile(
            platformCorner, &boardRamp, 1u);
        const auto westProfile = route1TerrainSharedEdgeProfile(
            boardRamp, &platformCorner, 3u);
        if (eastProfile.tileLevels !=
                std::array<std::int32_t, 2>{2, 2} ||
            eastProfile.neighborLevels !=
                std::array<std::int32_t, 2>{1, 2} ||
            westProfile.tileLevels !=
                std::array<std::int32_t, 2>{2, 1} ||
            westProfile.neighborLevels !=
                std::array<std::int32_t, 2>{2, 2}) {
            outFail =
                "Route 1 cell (16,-13) must retain the endpoint-resolved L2/L1-to-L2/L2 ledge beside the south-facing board ramp; collapsing this shared side to one scalar level produces a misplaced full-width cliff.";
            return false;
        }
    }
    {
        const TerrainTileState transition{
            .gridX = 14,
            .gridZ = -13,
            .elevationLevel = 2,
            .surface = "dark_lawn",
            .shape = "ramp_south",
            .sourceOccupied = true};
        const TerrainTileState westContinuation{
            .gridX = 13,
            .gridZ = -13,
            .elevationLevel = 2,
            .surface = "dark_lawn",
            .shape = "ramp_south",
            .sourceOccupied = true};
        const TerrainTileState northLowerGround{
            .gridX = 14,
            .gridZ = -12,
            .elevationLevel = 1,
            .surface = "light_lawn",
            .shape = "flat",
            .sourceOccupied = true};
        if (route1TerrainSourcePatchNeedsBoundarySpill(
                transition,
                &westContinuation,
                3u) ||
            !route1TerrainSourcePatchNeedsBoundarySpill(
                transition,
                &northLowerGround,
                0u)) {
            outFail =
                "Route 1 source patches must distinguish a continuous edge from a height-changing boundary before assigning donor ledge carriers.";
            return false;
        }
        const TerrainTileState loweredBoardRamp{
            .gridX = 14,
            .gridZ = -13,
            .elevationLevel = 1,
            .surface = "dirt_path",
            .shape = "ramp_south",
            .sourceOccupied = true,
            .authored = true};
        if (!route1TerrainSourceBoundaryInvalidated(
                loweredBoardRamp,
                &northLowerGround,
                transition,
                &northLowerGround,
                0u) ||
            route1TerrainSourceBoundaryInvalidated(
                transition,
                &northLowerGround,
                transition,
                &northLowerGround,
                0u)) {
            outFail =
                "Route 1 cleanup must identify when lowering a source ramp invalidates its complete old ledge band without retiring an unchanged source boundary.";
            return false;
        }
        const std::array<std::array<float, 3>, 3>
            undersideSpill{{
                {1500.086f, 50.0f, -1204.604f},
                {1600.043f, 66.875f, -1198.304f},
                {1500.086f, 66.875f, -1198.001f}}};
        auto boundaryOnly = undersideSpill;
        for (auto& position : boundaryOnly) {
            position[2] = -1200.0f;
        }
        if (!route1TerrainCleanupCarrierEntersNeighbor(
                undersideSpill,
                {15, -12},
                {15, -13}) ||
            route1TerrainCleanupCarrierEntersNeighbor(
                boundaryOnly,
                {15, -12},
                {15, -13}) ||
            route1TerrainCleanupCarrierEntersNeighbor(
                undersideSpill,
                {15, -12},
                {17, -12})) {
            outFail =
                "Route 1 cleanup carriers must identify real penetration into an adjacent tile without treating boundary vertices or non-adjacent cells as spill.";
            return false;
        }
        const std::array<std::array<float, 3>, 3>
            pairedUnderside{{
                {2100.043f, 66.875f, -1198.363f},
                {2000.043f, 50.0f, -1198.304f},
                {2100.043f, 50.0f, -1193.363f}}};
        auto distantGroundCard = pairedUnderside;
        for (auto& position : distantGroundCard) {
            position[2] += 35.0f;
        }
        if (!route1TerrainCleanupCarrierWithinBoundaryBand(
                pairedUnderside,
                {20, -13},
                {20, -12}) ||
            route1TerrainCleanupCarrierWithinBoundaryBand(
                distantGroundCard,
                {20, -13},
                {20, -12})) {
            outFail =
                "Route 1 exact ledge spill must retain the complete decoded 25 cm underside band without importing unrelated cleanup geometry deeper in the neighboring cell.";
            return false;
        }
        const std::array<std::array<float, 3>, 3>
            rebuiltLowerStorey{{
                {2500.0f, 0.0f, -300.0f},
                {2525.0f, 50.0f, -250.0f},
                {2500.0f, 49.99f, -200.0f}}};
        const std::array<std::array<float, 3>, 3>
            independentUpperStorey{{
                {2500.0f, 50.0f, -300.0f},
                {2600.0f, 100.0f, -250.0f},
                {2525.0f, 75.0f, -200.0f}}};
        if (!route1TerrainCleanupCarrierAtOrBelowBoundaryCeiling(
                rebuiltLowerStorey,
                50.0f) ||
            route1TerrainCleanupCarrierAtOrBelowBoundaryCeiling(
                independentUpperStorey,
                50.0f)) {
            outFail =
                "Every Route 1 cleanup ownership path must retire the rebuilt lower storey without erasing an independent source cliff that rises above its ceiling.";
            return false;
        }
        std::array<std::array<float, 3>, 3>
            canonicalCarrier{{
                {1392.911f, 95.033f, -1217.688f},
                {1402.092f, 100.0f, -1198.892f},
                {1393.307f, 95.033f, -1207.860f}}};
        route1TerrainClampCleanupCarrierToOwnedCell(
            canonicalCarrier,
            {13, -13},
            {14, -13});
        std::array<std::array<float, 3>, 3>
            donorCarrier{{
                {1888.682f, 149.222f, -1284.052f},
                {1913.890f, 175.120f, -1272.183f},
                {1903.903f, 99.664f, -1204.853f}}};
        route1TerrainClampCleanupCarrierToOwnedCell(
            donorCarrier,
            {19, -13},
            {18, -13});
        if (!close(canonicalCarrier[1][0], 1400.0f) ||
            !close(canonicalCarrier[1][1], 100.0f) ||
            !close(donorCarrier[0][0], 1900.0f) ||
            !close(donorCarrier[0][2], -1284.052f)) {
            outFail =
                "Route 1 matching cleanup carriers must be trimmed at their shared source-grid plane without changing height or longitudinal placement.";
            return false;
        }
        if (!route1TerrainMaskUsesAnyVertexOwnership(false) ||
            route1TerrainMaskUsesAnyVertexOwnership(true)) {
            outFail =
                "Route 1 ordinary terrain edits must remove every touching source carrier, while exact source-reference boundaries use centroid ownership before their retained carriers are trimmed to the shared plane.";
            return false;
        }
        TerrainTileState authoredTop{
            .gridX = 20,
            .gridZ = -10,
            .sourceElevationLevel = 1,
            .elevationLevel = 1,
            .sourceSurface = "light_lawn",
            .surface = "light_lawn",
            .shape = "flat",
            .sourceOccupied = true,
            .authored = true};
        TerrainTileState sourceTop{
            .gridX = 20,
            .gridZ = -9,
            .sourceElevationLevel = 1,
            .elevationLevel = 1,
            .sourceSurface = "light_lawn",
            .surface = "light_lawn",
            .shape = "flat",
            .sourceOccupied = true,
            .authored = false};
        if (!route1TerrainNeedsSourceSeamOverlap(
                authoredTop, &sourceTop, 0u)) {
            outFail =
                "A same-height authored/source lawn boundary must receive the shared seam overlap.";
            return false;
        }
        sourceTop.elevationLevel = 0;
        if (route1TerrainNeedsSourceSeamOverlap(
                authoredTop, &sourceTop, 0u)) {
            outFail =
                "A height-changing authored/source boundary must retain ledge ownership instead of a top-surface overlap.";
            return false;
        }
        authoredTop.elevationLevel = 2;
        authoredTop.shape = "flat";
        if (std::abs(route1TerrainProfileHeightCm(
                authoredTop, 0.35f, 0.65f) - 100.0f) >
                0.0001f) {
            outFail =
                "A flat Route 1 L2 tile must sample at exactly 100 source centimetres.";
            return false;
        }
        authoredTop.shape = "ramp_north";
        if (std::abs(route1TerrainProfileHeightCm(
                authoredTop, 0.25f, 0.6f) - 130.0f) >
                0.0001f) {
            outFail =
                "A north Route 1 ramp must continuously add one 50 cm level along local Z.";
            return false;
        }
        authoredTop.shape = "ramp_south";
        if (std::abs(route1TerrainProfileHeightCm(
                authoredTop, 0.25f, 0.6f) - 120.0f) >
                0.0001f) {
            outFail =
                "A south Route 1 ramp must continuously descend along local Z.";
            return false;
        }
        authoredTop.shape = "ramp_east";
        if (std::abs(route1TerrainProfileHeightCm(
                authoredTop, 0.25f, 0.6f) - 112.5f) >
                0.0001f) {
            outFail =
                "An east Route 1 ramp must continuously add one 50 cm level along local X.";
            return false;
        }
        authoredTop.shape = "ramp_west";
        if (std::abs(route1TerrainProfileHeightCm(
                authoredTop, 0.25f, 0.6f) - 137.5f) >
                0.0001f) {
            outFail =
                "A west Route 1 ramp must continuously descend along local X.";
            return false;
        }
    }
    {
        const auto lowEdge = route1SignRampDirtColor(0.0f, 0.0f);
        const auto lowCenter = route1SignRampDirtColor(0.0f, 0.5f);
        const auto middle = route1SignRampDirtColor(0.5f, 0.0f);
        const auto high = route1SignRampDirtColor(1.0f, 0.5f);
        const auto cleanFlatDirt = route1CleanFlatDirtColor();
        const auto cleanLightLawn = route1CleanLightLawnColor();
        const auto encounterSouthWest =
            route1EncounterGrassCoreTerrainCell(
                {2350.0f, 100.0f, -1550.0f}, {-1, -1});
        const auto encounterNorthEast =
            route1EncounterGrassCoreTerrainCell(
                {2350.0f, 100.0f, -1550.0f}, {1, 1});
        const auto encounterTintOffsets =
            route1EncounterGrassTintFootprintOffsets();
        if (!close(lowEdge[0], 0.905882359f) ||
            !close(lowEdge[1], 0.815686285f) ||
            !close(lowEdge[2], 0.631372571f) ||
            !close(lowEdge[3], 0.800000012f) ||
            !close(lowCenter[0], 0.984313726f) ||
            !close(lowCenter[1], 0.882352948f) ||
            !close(lowCenter[2], 0.686274529f) ||
            !close(lowCenter[3], 0.800000012f) ||
            !close(middle[0], 0.952941179f) ||
            !close(middle[3], 0.774509817f) ||
            !close(high[0], 1.0f) ||
            !close(high[1], 1.0f) ||
            !close(high[2], 1.0f) ||
            !close(high[3], 0.749019623f) ||
            !close(cleanFlatDirt[0], 0.905882359f) ||
            !close(cleanFlatDirt[1], 0.815686285f) ||
            !close(cleanFlatDirt[2], 0.631372571f) ||
            !close(cleanFlatDirt[3], 1.0f) ||
            cleanLightLawn !=
                std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f} ||
            encounterSouthWest !=
                std::array<std::int32_t, 2>{22, -17} ||
            encounterNorthEast !=
                std::array<std::int32_t, 2>{24, -15} ||
            encounterTintOffsets.front() !=
                std::array<std::int32_t, 2>{-1, -1} ||
            encounterTintOffsets[4] !=
                std::array<std::int32_t, 2>{0, 0} ||
            encounterTintOffsets.back() !=
                std::array<std::int32_t, 2>{1, 1} ||
            route1SignRampDirtColor(-1.0f, -1.0f) != lowEdge ||
            route1SignRampDirtColor(2.0f, 2.0f) != high) {
            outFail =
                "Route 1 editable ground no longer preserves the clean lawn/dirt controls, the source encounter-footprint grid mapping, or the sign-side ramp's exact Color0/Alpha_light profile.";
            return false;
        }
        constexpr std::array<float, 4> normalDirt{
            0.8f, 0.7f, 0.6f, 1.0f};
        const auto noTransition = route1SignRampAdjacentDirtColor(
            normalDirt, 0.0f, 0.5f, true);
        const auto highBoundary = route1SignRampAdjacentDirtColor(
            normalDirt, 1.0f, 0.5f, true);
        const auto lowBoundary = route1SignRampAdjacentDirtColor(
            normalDirt, 1.0f, 0.5f, false);
        const auto middleTransition = route1SignRampAdjacentDirtColor(
            normalDirt, 0.5f, 0.5f, true);
        if (noTransition != normalDirt ||
            !close(highBoundary[0], 1.0f) ||
            !close(highBoundary[3], 0.749019623f) ||
            !close(lowBoundary[0], 0.984313726f) ||
            !close(lowBoundary[3], 0.800000012f) ||
            !close(middleTransition[0], 0.9f) ||
            !close(middleTransition[3], 0.874509811f)) {
            outFail =
                "Route 1 dirt tiles no longer blend the sign-ramp Color0/Alpha_light field across adjacent flats.";
            return false;
        }
        constexpr std::array<float, 4> dirtField{
            0.70f, 0.63f, 0.42f, 0.80f};
        constexpr std::array<float, 4> neighboringLawn{
            0.88f, 0.97f, 0.73f, 1.0f};
        const auto dirtInterior =
            route1DirtAdjacentLawnColor(
                dirtField, neighboringLawn, 0.0f);
        const auto sharedLawnEdge =
            route1DirtAdjacentLawnColor(
                dirtField, neighboringLawn, 1.0f);
        const auto ribbonMidpoint =
            route1DirtAdjacentLawnColor(
                dirtField, neighboringLawn, 0.5f);
        if (dirtInterior != dirtField ||
            sharedLawnEdge != neighboringLawn ||
            !close(
                ribbonMidpoint[1],
                (dirtField[1] + neighboringLawn[1]) * 0.5f) ||
            !close(ribbonMidpoint[3], 0.90f)) {
            outFail =
                "Route 1 dirt grass ribbons must meet adjacent lawn with the lawn's Color0 and blend back to the dirt field across the recovered 30 cm band.";
            return false;
        }
        if (!close(route1DirtTransitionUv2V(0.0f), 0.928709f) ||
            !close(
                route1DirtTransitionUv2V(5.0f),
                0.932880402f) ||
            !close(
                route1DirtTransitionUv2V(30.0f),
                0.991155148f) ||
            !close(
                route1DirtTransitionUv2V(100.0f),
                0.991155148f)) {
            outFail =
                "Route 1 dirt boundaries must join the adjacent clean-lawn endpoint before traversing the recovered lawn/soil ribbon.";
            return false;
        }
    }
    {
        game::assets::published_environment::Mesh terrainMesh;
        terrainMesh.sourceIndex = 29u;
        terrainMesh.vertices.resize(6u);
        terrainMesh.vertices[0].position =
            {0.0f, 0.0f, 0.0f};
        terrainMesh.vertices[1].position =
            {100.0f, 48.0f, 0.0f};
        terrainMesh.vertices[2].position =
            {0.0f, 48.0f, 100.0f};
        terrainMesh.vertices[3].position =
            {1.0f, 33.0f, 1.0f};
        terrainMesh.vertices[4].position =
            {99.0f, 50.0f, 1.0f};
        terrainMesh.vertices[5].position =
            {1.0f, 50.0f, 99.0f};
        terrainMesh.polygonGroups = {
            game::assets::published_environment::PolygonGroup{
                .materialIndex = 18u,
                .primitiveType = "triangles",
                .indices = {0u, 1u, 2u}},
            game::assets::published_environment::PolygonGroup{
                .materialIndex = 13u,
                .primitiveType = "triangles",
                .indices = {3u, 4u, 5u}}};
        game::runtime::route1_terrain_assemblies::
            MeshPartition partition;
        std::string terrainError;
        if (!game::runtime::
                route1_terrain_assemblies::derivePartition(
                    terrainMesh,
                    partition,
                    &terrainError) ||
            partition.assemblies.size() != 1u ||
            partition.assemblies.front().polygonGroups.size() !=
                2u ||
            partition.assemblies.front().profileRole !=
                "source_ledge_or_raised_platform") {
            outFail =
                "Route 1 terrain should preserve one connected body/cap pair as one source assembly: " +
                terrainError;
            return false;
        }
    }
    {
        game::assets::published_environment::Mesh treeMesh;
        treeMesh.sourceIndex = 10u;
        treeMesh.vertices.resize(6u);
        treeMesh.vertices[0].position =
            {-10.0f, 0.0f, 0.0f};
        treeMesh.vertices[1].position =
            {10.0f, 0.0f, 0.0f};
        treeMesh.vertices[2].position =
            {0.0f, 30.0f, 0.0f};
        treeMesh.vertices[3].position =
            {190.0f, 5.0f, 0.0f};
        treeMesh.vertices[4].position =
            {210.0f, 5.0f, 0.0f};
        treeMesh.vertices[5].position =
            {200.0f, 35.0f, 0.0f};
        treeMesh.polygonGroups.push_back(
            game::assets::published_environment::PolygonGroup{
                .materialIndex = 2u,
                .primitiveType = "Triangles",
                .indices = {0u, 1u, 2u, 3u, 4u, 5u}});
        tree_instances::MeshPartition
            partition;
        std::string partitionError;
        if (!tree_instances::
                derivePartition(
                    treeMesh,
                    2u,
                    partition,
                    &partitionError) ||
            partition.sourcePivotsCm.size() != 2u ||
            !close(
                partition.sourcePivotsCm[0][1],
                0.0f) ||
            !close(
                partition.sourcePivotsCm[1][1],
                5.0f) ||
            tree_instances::
                    expectedInstanceCount(10u) !=
                11u ||
            tree_instances::
                    expectedInstanceCount(15u) !=
                9u) {
            outFail =
                "Route 1 tree source topology should derive stable, "
                "floor-aligned instance pivots: " +
                partitionError;
            return false;
        }
        std::vector<std::uint32_t> firstTreeIndices;
        std::vector<std::uint32_t> secondTreeIndices;
        if (!tree_instances::
                selectInstanceTriangles(
                    treeMesh,
                    partition.polygonGroups.front(),
                    0u,
                    firstTreeIndices,
                    &partitionError) ||
            !tree_instances::
                selectInstanceTriangles(
                    treeMesh,
                    partition.polygonGroups.front(),
                    1u,
                    secondTreeIndices,
                    &partitionError) ||
            firstTreeIndices !=
                std::vector<std::uint32_t>{
                    0u, 1u, 2u} ||
            secondTreeIndices !=
                std::vector<std::uint32_t>{
                    3u, 4u, 5u}) {
            outFail =
                "Route 1 tree instance selection should preserve exact "
                "source triangles: " +
                partitionError;
            return false;
        }
    }

    BoardLayoutTransform layout;
    std::string error;
    if (!loadBoardLayoutTransform(
            store,
            "layout.json",
            layout,
            &error)) {
        outFail =
            "Route 1 runtime should load an explicit board-layout "
            "transform: " +
            error;
        return false;
    }
    if (layout.declaredLocalDeltaCount != 0u ||
        layout.sourceUnitsToWorld != 0.01f) {
        outFail =
            "Route 1 runtime should preserve source scale and record the "
            "declared local-delta count.";
        return false;
    }

    const auto worldFromSource = worldFromSourceMatrix(layout);
    const auto sourceFromWorld = sourceFromWorldMatrix(layout);
    const auto mappedAnchor = transformPoint(
        worldFromSource,
        {2200.0f, 0.0f, -1700.0f, 1.0f});
    if (!close(mappedAnchor[0], 0.0f) ||
        !close(mappedAnchor[1], -0.04f) ||
        !close(mappedAnchor[2], 0.0f) ||
        !close(mappedAnchor[3], 1.0f)) {
        outFail =
            "Route 1 source anchor should map exactly to the manifest's "
            "gameplay-world anchor.";
        return false;
    }
    const auto roundTrip =
        transformPoint(sourceFromWorld, mappedAnchor);
    if (!close(roundTrip[0], 2200.0f) ||
        !close(roundTrip[1], 0.0f) ||
        !close(roundTrip[2], -1700.0f) ||
        !close(roundTrip[3], 1.0f)) {
        outFail =
            "Route 1 board registration must have an exact inverse for "
            "source-space shadow projection.";
        return false;
    }
    const std::array<float, 4> sourceSample{
        2450.0f, 125.0f, -2050.0f, 1.0f};
    const auto worldSample =
        transformPoint(worldFromSource, sourceSample);
    const auto projectionRows = route1CloudProjectionRows(layout);
    const auto canonicalUv =
        engine::render::route1_field_small_grass::
            projectRoute1CloudTextureUv(
                {sourceSample[0], sourceSample[1], sourceSample[2]});
    if (!close(dot(projectionRows.u, worldSample), canonicalUv[0]) ||
        !close(
            1.0f - dot(projectionRows.v, worldSample),
            canonicalUv[1])) {
        outFail =
            "Route 1 cloud projection must remain source-identical after "
            "the manifest-owned gameplay transform.";
        return false;
    }

    store.texts["layout_v2.json"] = R"json(
{
  "schema_version": 2,
  "kind": "route1_environment_board_layout",
  "coordinate_system": "source_centimetres_xyz_y_up",
  "source_profile_id": "route1_environment_road001_00",
  "source_to_world": {
    "source_units_to_world": 0.01,
    "source_anchor_cm": [2200.0, 0.0, -1700.0],
    "world_anchor": [0.0, -0.04, 0.0],
    "yaw_degrees": 0.0
  },
  "board_registration": {
    "board_cells": [8, 8]
  },
  "local_layout_deltas": [
    {
      "id": "autochess-board-clearance--flowers02-record-27",
      "target": {
        "kind": "buildmodel_vegetation_placement",
        "logical_name": "flowers02",
        "record_index": 27
      },
      "expected_source_transform": {
        "translation_cm": [2620.0, 150.0, -1870.0],
        "rotation_degrees": [0.0, -13.3333301544, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "authored_transform": {
        "translation_cm": [2620.0, 150.0, -1870.0],
        "rotation_degrees": [0.0, -13.3333301544, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "suppressed": true,
      "reason": "autochess_board_clearance"
    },
    {
      "id": "encounter-grass-layout-proof",
      "target": {
        "kind": "encounter_grass_record",
        "logical_name": "enc_grass01",
        "record_index": 0
      },
      "expected_source_transform": {
        "translation_cm": [1600.0, 50.0, -900.0],
        "rotation_degrees": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "authored_transform": {
        "translation_cm": [1612.5, 50.0, -900.0],
        "rotation_degrees": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "suppressed": false,
      "reason": "editor_contract_test"
    },
    {
      "id": "canonical-mesh-layout-proof",
      "target": {
        "kind": "canonical_mesh_group",
        "logical_name": "route_ground_plane",
        "record_index": 36
      },
      "expected_source_transform": {
        "translation_cm": [0.0, 0.0, 0.0],
        "rotation_degrees": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "authored_transform": {
        "translation_cm": [0.0, 0.0, 0.0],
        "rotation_degrees": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "suppressed": false,
      "reason": "editor_contract_test"
    },
    {
      "id": "canonical-tree-layout-proof",
      "target": {
        "kind": "canonical_tree_instance",
        "logical_name": "tree_001",
        "record_index": 4
      },
      "expected_source_transform": {
        "translation_cm": [1840.0, 50.0, -1170.0],
        "rotation_degrees": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "authored_transform": {
        "translation_cm": [1850.0, 50.0, -1170.0],
        "rotation_degrees": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "suppressed": false,
      "reason": "editor_contract_test"
    }
  ]
}
)json";
    if (!loadBoardLayoutTransform(
            store,
            "layout_v2.json",
            layout,
            &error) ||
        layout.boardCells !=
            std::array<std::uint32_t, 2>{8u, 8u} ||
        layout.localLayoutDeltas.size() != 4u ||
        !layout.localLayoutDeltas.front().suppressed ||
        layout.localLayoutDeltas.front().recordIndex != 27u ||
        layout.localLayoutDeltas[1].targetKind !=
            "encounter_grass_record" ||
        layout.localLayoutDeltas[2].targetKind !=
            "canonical_mesh_group" ||
        layout.localLayoutDeltas[3].targetKind !=
            "canonical_tree_instance") {
        outFail =
            "Route 1 runtime should decode all supported stable, "
            "source-guarded local layout targets: " +
            error;
        return false;
    }
    MemoryAssetStore roundTripStore;
    layout.boardCellSizeWorld = 1.0f;
    layout.benchSlots = 8u;
    layout.benchGapCells = 0u;
    layout.northBench = true;
    layout.southBench = true;
    layout.objectMetadataOverrides.push_back(
        LayoutObjectMetadataOverride{
            .stableId =
                "canonical-tree/tree_001/instance-4",
            .displayName = "North Gate Tree 5",
            .categoryPath =
                "Environment/Vegetation/Trees/North Gate"});
    layout.authoredPrefabInstances.push_back(
        AuthoredPrefabInstance{
            .stableId =
                "authored-prefab/canonical-tree-tree-001-instance-4/copy-1",
            .prototypeStableId =
                "canonical-tree/tree_001/instance-4",
            .displayName = "North Gate Tree 5 Copy 1",
            .categoryPath =
                "Environment/Vegetation/Trees/North Gate",
            .sourceTranslationCm =
                {1850.0f, 50.0f, -1170.0f},
            .sourceRotationDegrees =
                {0.0f, 0.0f, 0.0f},
            .sourceScale = {1.0f, 1.0f, 1.0f},
            .translationCm =
                {1950.0f, 50.0f, -1170.0f},
            .rotationDegrees =
                {0.0f, 15.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .reason = "editor_contract_test"});
    layout.authoredTerrainTiles.push_back(
        AuthoredTerrainTile{
            .stableId = route1TerrainTileStableId(22, -17),
            .displayName = "Terrain Tile (22, -17)",
            .categoryPath = "Environment/Terrain/Tiles",
            .tileSetAssetId = "route1/terrain_tileset",
            .gridX = 22,
            .gridZ = -17,
            .elevationLevel = 1,
            .surface = "dark_lawn",
            .shape = "ramp_north",
            .reason = "editor_contract_test"});
    roundTripStore.texts["roundtrip.json"] =
        serializeBoardLayoutTransform(layout);
    BoardLayoutTransform roundTripLayout;
    if (!loadBoardLayoutTransform(
            roundTripStore,
            "roundtrip.json",
            roundTripLayout,
            &error) ||
        !roundTripLayout.localLayoutDeltas.empty() ||
        !roundTripLayout.objectMetadataOverrides.empty() ||
        !roundTripLayout.authoredPrefabInstances.empty() ||
        !roundTripLayout.authoredTerrainTiles.empty() ||
        std::abs(roundTripLayout.boardCellSizeWorld - 1.0f) >
            0.0001f ||
        roundTripLayout.terrainGridOrigin !=
            std::array<std::int32_t, 2>{18, -21} ||
        roundTripLayout.terrainElevationLevel != 0 ||
        northBenchTerrainGridOrigin(roundTripLayout) !=
            std::array<std::int32_t, 2>{18, -13} ||
        southBenchTerrainGridOrigin(roundTripLayout) !=
            std::array<std::int32_t, 2>{18, -22} ||
        roundTripLayout.benchSlots != 8u ||
        roundTripLayout.benchGapCells != 0u ||
        !roundTripLayout.northBench ||
        !roundTripLayout.southBench ||
        roundTripStore.texts["roundtrip.json"].find(
            "local_layout_deltas") != std::string::npos ||
        roundTripStore.texts["roundtrip.json"].find(
            "authored_prefab_instances") != std::string::npos ||
        roundTripStore.texts["roundtrip.json"].find(
            "authored_terrain_tiles") != std::string::npos ||
        roundTripStore.texts["roundtrip.json"].find(
            "source_anchor_cm") != std::string::npos ||
        roundTripStore.texts["roundtrip.json"].find(
            "cell_size_world") != std::string::npos ||
        roundTripStore.texts["roundtrip.json"].find(
            "terrain_grid_origin") == std::string::npos ||
        roundTripStore.texts["roundtrip.json"].find(
            "gameplay_footprint_terrain_grid_bound") ==
            std::string::npos ||
        roundTripStore.texts["roundtrip.json"].find(
            "1.000000") != std::string::npos) {
        outFail =
            "Route 1 board serialization must own only global board "
            "registration; object authoring belongs to the generic "
            "authored-scene document: " +
            error;
        return false;
    }

    store.texts["unbound_board.json"] = R"json(
{
  "schema_version": 5,
  "kind": "route1_environment_board_layout",
  "coordinate_system": "source_centimetres_xyz_y_up",
  "source_profile_id": "route1_environment_road001_00",
  "source_to_world": {
    "source_units_to_world": 0.01,
    "source_anchor_cm": [2200.0, 0.0, -1700.0],
    "world_anchor": [0.0, -0.04, 0.0],
    "yaw_degrees": 0.0
  },
  "board_registration": {
    "board_cells": [8, 8],
    "cell_size_world": 1.2,
    "bench_slots": 8,
    "bench_sides": ["north", "south"]
  }
}
)json";
    if (loadBoardLayoutTransform(
            store,
            "unbound_board.json",
            layout,
            &error)) {
        outFail =
            "Route 1 must reject a gameplay board whose cell size can drift from the one-metre terrain lattice.";
        return false;
    }

    store.texts["schema6_float_escape.json"] = R"json(
{
  "schema_version": 6,
  "kind": "route1_environment_board_layout",
  "coordinate_system": "source_centimetres_xyz_y_up",
  "source_profile_id": "route1_environment_road001_00",
  "source_to_world": {
    "source_units_to_world": 0.01,
    "source_anchor_cm": [2200.0, 0.0, -1700.0],
    "world_anchor": [0.0, -0.04, 0.0],
    "yaw_degrees": 0.0
  },
  "board_registration": {
    "board_cells": [8, 8],
    "terrain_grid_origin": [18, -21],
    "terrain_elevation_level": 0,
    "terrain_tile_size_cm": 100.0,
    "bench_slots": 8,
    "bench_gap_cells": 1,
    "bench_sides": ["north", "south"]
  }
}
)json";
    if (loadBoardLayoutTransform(
            store,
            "schema6_float_escape.json",
            layout,
            &error)) {
        outFail =
            "Schema 6 must reject an independently stored floating-point board anchor.";
        return false;
    }

    store.texts["bad_layout.json"] = R"json(
{
  "schema_version": 1,
  "kind": "lgpe_route1_board_layout_delta",
  "coordinate_system": "guessed_world_units",
  "source_profile_id": "lgpe_route1_road001_00",
  "source_to_world": {
    "source_units_to_world": 0.01,
    "source_anchor_cm": [0, 0, 0],
    "world_anchor": [0, 0, 0],
    "yaw_degrees": 0
  },
  "local_layout_deltas": []
}
)json";
    if (loadBoardLayoutTransform(
            store,
            "bad_layout.json",
            layout,
            &error)) {
        outFail =
            "Route 1 runtime should reject an implicit or guessed source "
            "coordinate system.";
        return false;
    }
    return true;
}
