#pragma once
// Source-preservation rules for the original editor-authored route references.
// Blender-authored terrain does not use these repair decisions.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
namespace game::runtime::route1_environment {
struct TerrainTileState;
inline constexpr bool route1UsesRegionalTerrainMaterialField(
    std::string_view sceneId) noexcept {
    // South Entrance (`routes/route1`) is the pinned approved source/edited
    // composition. Every additional Route 1 board location follows the
    // `routes/route1-*` naming contract and should inherit the connected
    // regional material solve automatically rather than requiring another
    // scene-specific renderer exception.
    return sceneId.starts_with("routes/route1-");
}

bool route1TerrainPinsSourceSurface(
    const TerrainTileState &tile) noexcept;

bool route1TerrainBelongsToUnchangedSourceLedgeAssembly(
    const TerrainTileState &tile,
    const std::vector<TerrainTileState> &activeTiles,
    const std::vector<TerrainTileState> &sourceTiles) noexcept;

bool route1TerrainUsesExactSourceSurfaceOverride(
    const TerrainTileState &tile,
    const std::vector<TerrainTileState> &activeTiles,
    const std::vector<TerrainTileState> &sourceTiles) noexcept;

bool route1TerrainUsesRegionalExactSourceLawnMaterial(
    std::string_view sceneId,
    const TerrainTileState &tile,
    const std::vector<TerrainTileState> &activeTiles,
    const std::vector<TerrainTileState> &sourceTiles) noexcept;

bool route1TerrainCanPreserveRelativeSourceGeometry(
    const TerrainTileState &tile,
    const std::vector<TerrainTileState> &activeTiles,
    const std::vector<TerrainTileState> &sourceTiles) noexcept;

float route1TerrainProfileHeightCm(
    const TerrainTileState &tile,
    float localX,
    float localZ) noexcept;

bool route1TerrainSourceBoundaryInvalidated(
    const TerrainTileState &editedTile,
    const TerrainTileState *editedNeighbor,
    const TerrainTileState &sourceTile,
    const TerrainTileState *sourceNeighbor,
    std::size_t edge) noexcept;

bool route1TerrainSourcePatchNeedsBoundarySpill(
    const TerrainTileState &tile,
    const TerrainTileState *neighbor,
    std::size_t edge) noexcept;

bool route1TerrainCleanupCarrierEntersNeighbor(
    const std::array<std::array<float, 3>, 3> &positionsCm,
    const std::array<std::int32_t, 2> &ownerCell,
    const std::array<std::int32_t, 2> &neighboringCell) noexcept;

bool route1TerrainCleanupCarrierWithinBoundaryBand(
    const std::array<std::array<float, 3>, 3> &positionsCm,
    const std::array<std::int32_t, 2> &ownerCell,
    const std::array<std::int32_t, 2> &neighboringCell) noexcept;

bool route1TerrainCleanupCarrierIntersectsBoundaryBand(
    const std::array<std::array<float, 3>, 3> &positionsCm,
    const std::array<std::int32_t, 2> &ownerCell,
    const std::array<std::int32_t, 2> &neighboringCell) noexcept;

bool route1TerrainCleanupCarrierWithinRebuiltBoundaryCorridor(
    const std::array<std::array<float, 3>, 3> &positionsCm,
    const std::array<std::int32_t, 2> &ownerCell,
    const std::array<std::int32_t, 2> &neighboringCell) noexcept;

bool route1TerrainCleanupCarrierAtOrBelowBoundaryCeiling(
    const std::array<std::array<float, 3>, 3> &positionsCm,
    float boundaryCeilingCm) noexcept;

bool route1TerrainCleanupCarrierIntersectsCellFootprint(
    const std::array<std::array<float, 3>, 3> &positionsCm,
    const std::array<std::int32_t, 2> &cell) noexcept;

void route1TerrainClampCleanupCarrierToOwnedCell(
    std::array<std::array<float, 3>, 3> &positionsCm,
    const std::array<std::int32_t, 2> &ownerCell,
    const std::array<std::int32_t, 2> &neighboringCell) noexcept;

bool route1TerrainMaskUsesAnyVertexOwnership(
    bool exactSourceReference) noexcept;
} // namespace game::runtime::route1_environment
