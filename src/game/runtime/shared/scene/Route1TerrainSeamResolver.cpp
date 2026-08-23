#include "game/runtime/shared/scene/Route1TerrainSeamResolver.h"

#include <array>
#include <cstddef>
#include <map>
#include <queue>
#include <utility>

namespace game::runtime::route1_terrain_seams {
namespace {

using route1_environment::TerrainTileState;
using GridCell = std::pair<std::int32_t, std::int32_t>;

constexpr std::array<std::array<std::int32_t, 2>, 4> kDirections{{
    {0, 1},
    {1, 0},
    {0, -1},
    {-1, 0},
}};

bool activeSurface(const TerrainTileState& tile) {
    return tile.surface != "empty" &&
        (tile.sourceOccupied || tile.authored);
}

bool startsContinuousField(const TerrainTileState& tile) {
    if (!activeSurface(tile) || tile.sourceReference) {
        return false;
    }
    if (tile.cleanSuppressedEncounterGrassTint ||
        tile.normalizeSourceTint) {
        return true;
    }
    return tile.authored &&
        (tile.elevationLevel != tile.sourceElevationLevel ||
         tile.shape != tile.sourceShape ||
         tile.surface != tile.sourceSurface);
}

bool compatibleFieldNeighbor(
    const TerrainTileState& tile,
    const TerrainTileState& neighbor,
    std::size_t edge) {
    if (!activeSurface(neighbor) ||
        neighbor.sourceReference ||
        neighbor.surface != tile.surface) {
        return false;
    }
    const auto profile =
        route1_environment::route1TerrainSharedEdgeProfile(
            tile, &neighbor, edge);
    return profile.tileLevels == profile.neighborLevels;
}

} // namespace

Resolution resolve(std::vector<TerrainTileState>& tiles) {
    Resolution resolution;
    std::map<GridCell, std::size_t> tileByCell;
    for (std::size_t index = 0u; index < tiles.size(); ++index) {
        auto& tile = tiles[index];
        tile.rebuildContinuousMaterialFields = false;
        tile.projectedShadowMismatchEdgeMask = 0u;
        tileByCell.emplace(
            GridCell{tile.gridX, tile.gridZ}, index);
    }

    std::queue<std::size_t> pending;
    for (std::size_t index = 0u; index < tiles.size(); ++index) {
        if (!startsContinuousField(tiles[index])) {
            continue;
        }
        tiles[index].rebuildContinuousMaterialFields = true;
        pending.push(index);
    }

    // A topology/material edit owns one continuous UV0/UV1 field across its
    // compatible authored component. Untouched source cells remain exact and
    // are joined by the existing source-overlap boundary carrier.
    while (!pending.empty()) {
        const std::size_t index = pending.front();
        pending.pop();
        const TerrainTileState& tile = tiles[index];
        for (std::size_t edge = 0u; edge < kDirections.size(); ++edge) {
            const auto direction = kDirections[edge];
            const auto found = tileByCell.find(
                {tile.gridX + direction[0],
                 tile.gridZ + direction[1]});
            if (found == tileByCell.end()) {
                continue;
            }
            auto& neighbor = tiles[found->second];
            if (neighbor.rebuildContinuousMaterialFields ||
                !neighbor.authored ||
                !compatibleFieldNeighbor(tile, neighbor, edge)) {
                continue;
            }
            neighbor.rebuildContinuousMaterialFields = true;
            pending.push(found->second);
        }
    }

    // Scan only north/east ownership edges so each diagnostic is counted
    // exactly once; both tiles receive a directional mask for the overlay.
    for (std::size_t index = 0u; index < tiles.size(); ++index) {
        auto& tile = tiles[index];
        if (!activeSurface(tile)) {
            continue;
        }
        for (std::size_t edge = 0u; edge < 2u; ++edge) {
            const auto direction = kDirections[edge];
            const auto found = tileByCell.find(
                {tile.gridX + direction[0],
                 tile.gridZ + direction[1]});
            if (found == tileByCell.end()) {
                continue;
            }
            auto& neighbor = tiles[found->second];
            if ((!tile.authored && !neighbor.authored) ||
                !compatibleFieldNeighbor(tile, neighbor, edge) ||
                tile.receivesProjectedShadow ==
                    neighbor.receivesProjectedShadow) {
                continue;
            }
            tile.projectedShadowMismatchEdgeMask |=
                static_cast<std::uint8_t>(1u << edge);
            const std::size_t oppositeEdge = (edge + 2u) % 4u;
            neighbor.projectedShadowMismatchEdgeMask |=
                static_cast<std::uint8_t>(1u << oppositeEdge);
            ++resolution.projectedShadowMismatchEdgeCount;
        }
    }

    for (const auto& tile : tiles) {
        if (tile.rebuildContinuousMaterialFields) {
            ++resolution.continuousFieldCellCount;
        }
    }
    return resolution;
}

} // namespace game::runtime::route1_terrain_seams
