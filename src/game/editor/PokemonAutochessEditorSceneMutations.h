#pragma once

#include "engine/editor/EditorProjectPlugin.h"
#include "game/editor/PokemonAutochessEditorCommands.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"

#include <array>
#include <cstddef>
#include <cstdint>
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

game::runtime::lgpe_route1_runtime::BoardLayoutTransform
boardRegistrationFromCenter(
    const game::runtime::lgpe_route1_runtime::BoardLayoutTransform&
        currentLayout,
    const std::array<float, 3>& requestedCenterCm,
    float terrainTileSizeCm,
    float terrainElevationStepCm);

game::runtime::lgpe_route1_runtime::BoardLayoutTransform
defaultBoardRegistration(
    const game::runtime::lgpe_route1_runtime::BoardLayoutTransform&
        currentLayout,
    const std::array<std::int32_t, 2>& defaultTerrainGridOrigin);

bool sameBoardRegistration(
    const game::runtime::lgpe_route1_runtime::BoardLayoutTransform& left,
    const game::runtime::lgpe_route1_runtime::BoardLayoutTransform& right)
    noexcept;

game::runtime::lgpe_route1_runtime::BoardLayoutTransform
importedSceneBaseline(
    const game::runtime::lgpe_route1_runtime::BoardLayoutTransform&
        currentLayout);

struct BoardClearanceConfig {
    float boardCellSizeWorld = 1.0f;
    float terrainTileSizeCm = 100.0f;
    float terrainElevationStepCm = 50.0f;
    std::string_view groundPrototypeStableId;
    std::string_view groundPrefabAssetId;
    std::string_view groundInstanceStableId;
    std::string_view terrainTileSetAssetId;
};

struct BoardClearancePlan {
    commands::BoardClearanceResult result;
    std::vector<std::string> suppressStableIds;
    std::vector<
        game::runtime::lgpe_route1_runtime::AuthoredTerrainTile>
        groundInfillTiles;
};

bool buildBoardClearancePlan(
    const commands::BoardClearanceRequest& request,
    const game::runtime::lgpe_route1_runtime::BoardLayoutTransform&
        currentLayout,
    const std::vector<
        game::runtime::lgpe_route1_runtime::LayoutObject>& objects,
    const std::vector<
        game::runtime::lgpe_route1_runtime::TerrainTileState>& terrainTiles,
    const BoardClearanceConfig& config,
    BoardClearancePlan& outPlan,
    std::string* outError = nullptr);

} // namespace game::editor::scene_mutations
