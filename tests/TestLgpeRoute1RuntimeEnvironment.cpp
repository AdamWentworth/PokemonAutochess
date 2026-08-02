#include "engine/core/IAssetStore.h"
#include "engine/render/LgpeFieldSmallGrassMaterial.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"
#include "game/runtime/shared/scene/LgpeRoute1TerrainAssemblies.h"
#include "game/runtime/shared/scene/LgpeRoute1TreeInstances.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

namespace tree_instances =
    game::runtime::lgpe_route1_tree_instances;

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

bool test_lgpe_route1_runtime_environment_contract(std::string& outFail) {
    MemoryAssetStore store;
    store.texts["layout.json"] = R"json(
{
  "schema_version": 1,
  "kind": "lgpe_route1_board_layout_delta",
  "coordinate_system": "source_centimetres_xyz_y_up",
  "source_profile_id": "lgpe_route1_road001_00",
  "source_to_world": {
    "source_units_to_world": 0.01,
    "source_anchor_cm": [2200.0, 0.0, -1700.0],
    "world_anchor": [0.0, -0.04, 0.0],
    "yaw_degrees": 0.0
  },
  "local_layout_deltas": []
}
)json";

    using namespace game::runtime::lgpe_route1_runtime;
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
        if (!route1TerrainSourcePatchSharesCleanupCarrierPair(
                transition,
                &westContinuation,
                3u) ||
            route1TerrainSourcePatchSharesCleanupCarrierPair(
                transition,
                &northLowerGround,
                0u)) {
            outFail =
                "Route 1 exact source patches must replace both halves of a matching cleanup-carrier pair without claiming a height-changing ledge boundary.";
            return false;
        }
        if (!route1TerrainMaskUsesAnyVertexOwnership(false) ||
            route1TerrainMaskUsesAnyVertexOwnership(true)) {
            outFail =
                "Route 1 ordinary terrain edits must remove every touching source carrier, while exact source-reference patches and their compatible cleanup halos use centroid ownership so a carrier pair is never sliced.";
            return false;
        }
    }
    {
        const auto lowEdge = route1SignRampDirtColor(0.0f, 0.0f);
        const auto lowCenter = route1SignRampDirtColor(0.0f, 0.5f);
        const auto middle = route1SignRampDirtColor(0.5f, 0.0f);
        const auto high = route1SignRampDirtColor(1.0f, 0.5f);
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
            route1SignRampDirtColor(-1.0f, -1.0f) != lowEdge ||
            route1SignRampDirtColor(2.0f, 2.0f) != high) {
            outFail =
                "Route 1 editable dirt ramps no longer preserve the sign-side source ramp's exact Color0/Alpha_light profile.";
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
    }
    {
        engine::assets::lgpe::Mesh terrainMesh;
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
            engine::assets::lgpe::PolygonGroup{
                .materialIndex = 18u,
                .primitiveType = "triangles",
                .indices = {0u, 1u, 2u}},
            engine::assets::lgpe::PolygonGroup{
                .materialIndex = 13u,
                .primitiveType = "triangles",
                .indices = {3u, 4u, 5u}}};
        game::runtime::lgpe_route1_terrain_assemblies::
            MeshPartition partition;
        std::string terrainError;
        if (!game::runtime::
                lgpe_route1_terrain_assemblies::derivePartition(
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
        engine::assets::lgpe::Mesh treeMesh;
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
            engine::assets::lgpe::PolygonGroup{
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
        engine::render::lgpe_field_small_grass::
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
  "kind": "lgpe_route1_board_layout_delta",
  "coordinate_system": "source_centimetres_xyz_y_up",
  "source_profile_id": "lgpe_route1_road001_00",
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
    layout.benchGapCells = 1u;
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
            std::array<std::int32_t, 2>{18, -12} ||
        southBenchTerrainGridOrigin(roundTripLayout) !=
            std::array<std::int32_t, 2>{18, -23} ||
        roundTripLayout.benchSlots != 8u ||
        roundTripLayout.benchGapCells != 1u ||
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
  "kind": "lgpe_route1_board_layout_delta",
  "coordinate_system": "source_centimetres_xyz_y_up",
  "source_profile_id": "lgpe_route1_road001_00",
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
  "kind": "lgpe_route1_board_layout_delta",
  "coordinate_system": "source_centimetres_xyz_y_up",
  "source_profile_id": "lgpe_route1_road001_00",
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
