#pragma once

#include "engine/editor/EditorProjectPlugin.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace game::editor::scene_mutations {

struct TerrainTileEditResult {
    game::runtime::lgpe_route1_runtime::BoardLayoutTransform layout;
    std::size_t affectedTileCount = 0u;
};

bool buildTerrainTileEdit(
    const engine::editor::EditorProjectTerrainTileEditRequest& request,
    const std::vector<
        game::runtime::lgpe_route1_runtime::TerrainTileState>& terrainTiles,
    const game::runtime::lgpe_route1_runtime::BoardLayoutTransform&
        currentLayout,
    std::string_view terrainTileSetAssetId,
    TerrainTileEditResult& outResult,
    std::string* outError = nullptr);

} // namespace game::editor::scene_mutations
