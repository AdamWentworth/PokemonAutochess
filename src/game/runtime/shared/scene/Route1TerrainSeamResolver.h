#pragma once

#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"

#include <cstdint>
#include <vector>

namespace game::runtime::route1_terrain_seams {

struct Resolution {
    std::uint32_t continuousFieldCellCount = 0u;
    std::uint32_t projectedShadowMismatchEdgeCount = 0u;
};

// Resolves derived material-field ownership for the complete edited terrain
// graph. The authored document remains untouched: these flags are rebuilt on
// every preview/commit so rendering and diagnostics cannot drift apart.
Resolution resolve(
    std::vector<route1_environment::TerrainTileState>& tiles);

} // namespace game::runtime::route1_terrain_seams
