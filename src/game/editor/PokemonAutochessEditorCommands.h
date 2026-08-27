#pragma once

#include "engine/editor/EditorProjectPlugin.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace game::editor::commands {

inline constexpr std::string_view kClearBoardFootprintId =
    "pokemonautochess.route1.clear_board_footprint";
inline constexpr std::string_view kResetImportedSceneId =
    "pokemonautochess.route1.reset_imported_scene";
inline constexpr std::string_view kToggleTerrainSeamDiagnosticsId =
    "pokemonautochess.route1.toggle_terrain_seam_diagnostics";
inline constexpr std::string_view kToggleTerrainPatchV2PreviewId =
    "pokemonautochess.route1.toggle_terrain_patch_v2_preview";

enum class Kind : std::uint8_t {
    Unknown = 0u,
    ClearBoardFootprint,
    ResetImportedScene,
    ToggleTerrainSeamDiagnostics,
    ToggleTerrainPatchV2Preview,
};

struct BoardClearanceRequest {
    float paddingCells = 0.35f;
    bool clearTerrain = true;
    bool clearVegetation = true;
    bool clearObjects = true;
    bool retainRamps = true;
    bool addGroundInfill = true;
};

struct BoardClearanceResult {
    std::uint32_t suppressedTerrainCount = 0u;
    std::uint32_t suppressedVegetationCount = 0u;
    std::uint32_t suppressedObjectCount = 0u;
    std::uint32_t retainedRampCount = 0u;
    std::uint32_t skippedUnsafeAggregateCount = 0u;
    bool groundInfillCreated = false;
};

std::size_t count(bool route1Available) noexcept;
engine::editor::EditorProjectCommand command(
    bool route1Available,
    std::size_t index) noexcept;
Kind resolve(std::string_view commandId) noexcept;

bool parseBoardClearanceRequest(
    const engine::editor::EditorProjectCommandValue* values,
    std::size_t valueCount,
    BoardClearanceRequest& outRequest,
    std::string* outError = nullptr);
std::string boardClearanceStatus(
    const BoardClearanceResult& result);
std::string unknownCommandError(std::string_view commandId);

} // namespace game::editor::commands
