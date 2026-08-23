#pragma once

#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace game::runtime::route1_terrain_ledges {

using GridCell = std::pair<std::int32_t, std::int32_t>;

struct RebuiltEdge {
    GridCell ownerCell{};
    std::size_t edge = 0u;
    route1_environment::TerrainSharedEdgeProfile profile;
    std::uint32_t contourIndex = 0u;
    float contourStartCm = 0.0f;
};

struct Resolution {
    std::vector<RebuiltEdge> edges;
    std::uint32_t contourCount = 0u;
};

// Derives every cliff/fringe edge invalidated by authored terrain and chains
// compatible directed edges into deterministic contours. Render geometry uses
// contourStartCm instead of restarting or mirroring its texture field per tile.
Resolution resolve(
    const std::vector<route1_environment::TerrainTileState>& tiles,
    const std::vector<route1_environment::TerrainTileState>& sourceTiles,
    const std::set<GridCell>& cleanupCells);

const RebuiltEdge* find(
    const Resolution& resolution,
    GridCell ownerCell,
    std::size_t edge) noexcept;

} // namespace game::runtime::route1_terrain_ledges
