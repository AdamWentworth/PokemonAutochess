#include "game/editor/PokemonAutochessEditorHierarchy.h"

#include <algorithm>

namespace game::editor::hierarchy {

std::size_t objectCount(
    bool sceneViewReady,
    std::size_t environmentObjectCount,
    std::size_t previewUnitCount) noexcept {
    return sceneViewReady
        ? 1u + environmentObjectCount + previewUnitCount
        : 0u;
}

ObjectAddress resolveObjectAddress(
    bool sceneViewReady,
    std::size_t environmentObjectCount,
    std::size_t previewUnitCount,
    std::size_t hierarchyIndex) noexcept {
    if (!sceneViewReady) {
        return {};
    }
    if (hierarchyIndex == 0u) {
        return {ObjectDomain::GameplayBoard, 0u};
    }
    --hierarchyIndex;
    if (hierarchyIndex < environmentObjectCount) {
        return {ObjectDomain::Environment, hierarchyIndex};
    }
    hierarchyIndex -= environmentObjectCount;
    if (hierarchyIndex < previewUnitCount) {
        return {ObjectDomain::GameplayPreviewUnit, hierarchyIndex};
    }
    return {};
}

void Selection::select(const char* stableId) {
    stableId_ = stableId ? stableId : "";
}

void Selection::select(std::string_view stableId) {
    stableId_.assign(stableId);
}

void Selection::clear() noexcept {
    stableId_.clear();
}

const std::string& Selection::id() const noexcept {
    return stableId_;
}

bool Selection::empty() const noexcept {
    return stableId_.empty();
}

bool Selection::matches(std::string_view stableId) const noexcept {
    return stableId_ == stableId;
}

engine::editor::EditorProjectLayoutObject environmentObjectView(
    const game::runtime::route1_environment::LayoutObject& object) noexcept {
    return {
        .stableId = object.stableId.c_str(),
        .displayName = object.displayName.c_str(),
        .typeName =
            object.authored
            ? "Authored Prefab Instance"
            : object.targetKind == "canonical_terrain_assembly"
            ? "Source Terrain Assembly"
            : object.targetKind == "canonical_mesh_group"
            ? "Source Mesh Group"
            : object.targetKind == "gameplay_board_ground_prototype"
            ? "Gameplay Ground Prefab"
            : object.targetKind == "canonical_tree_instance"
            ? "Tree Prefab Placement"
            : object.targetKind == "encounter_grass_record"
            ? "Encounter Grass Prefab Placement"
            : "Environment Prefab Placement",
        .coordinateSystem = "Source centimetres (XYZ, Y-up)",
        .reason = object.reason.c_str(),
        .targetKind = object.targetKind.c_str(),
        .categoryPath = object.categoryPath.c_str(),
        .prefabAssetId = object.prefabAssetId.c_str(),
        .sourceTranslation = object.sourceTranslationCm,
        .sourceRotationDegrees = object.sourceRotationDegrees,
        .sourceScale = object.sourceScale,
        .translation = object.translationCm,
        .rotationDegrees = object.rotationDegrees,
        .scale = object.scale,
        .boundsMinimum = object.boundsMinimumCm,
        .boundsMaximum = object.boundsMaximumCm,
        .suppressed = object.suppressed,
        .hasOverride = object.hasOverride};
}

engine::editor::EditorProjectLayoutObject gameplayBoardView(
    const game::runtime::route1_environment::BoardLayoutTransform& layout,
    const GameplayBoardViewConfig& config) noexcept {
    const float scale = layout.boardCellSizeWorld;
    const float sourceCellSize =
        scale / std::max(0.0001f, layout.sourceUnitsToWorld);
    const float halfWidth =
        static_cast<float>(layout.boardCells[0]) *
        sourceCellSize * 0.5f;
    const float halfDepth =
        static_cast<float>(layout.boardCells[1]) *
        sourceCellSize * 0.5f;
    const float gameplayHalfWidth = std::max(
        halfWidth,
        static_cast<float>(layout.benchSlots) *
            sourceCellSize * 0.5f);
    const float benchOffset =
        static_cast<float>(layout.benchGapCells + 1u) *
        sourceCellSize;
    const float gameplayMinimumZ =
        layout.sourceAnchorCm[2] - halfDepth -
        (layout.southBench ? benchOffset : 0.0f);
    const float gameplayMaximumZ =
        layout.sourceAnchorCm[2] + halfDepth +
        (layout.northBench ? benchOffset : 0.0f);

    engine::editor::EditorProjectLayoutObject view{
        .stableId = config.stableId,
        .displayName = "Autochess Board + Benches",
        .typeName = "Gameplay Board Layout",
        .coordinateSystem = "Exact Route 1 terrain-cell coordinates",
        .reason = "gameplay_board_registration",
        .targetKind = "gameplay_board",
        .categoryPath = "Gameplay/Board",
        .prefabAssetId = "",
        .inspectorTitle = "Gameplay Board Layout",
        .inspectorSummary =
            "PokemonAutochess 8x8 board with north and south bench rows",
        .viewportHint =
            "Scene viewport: select the board marker and use Move [W]. PokemonAutochess keeps its board and benches bound to Route 1 terrain cells.",
        .resetLabel = "Reset Board Registration",
        .scaleReadOnlyLabel = "Board tile size",
        .scaleReadOnlyDescription =
            "PokemonAutochess binds one board cell to one Route 1 terrain cell; rotation and scale cannot drift from that lattice.",
        .capabilities =
            engine::editor::EditorProjectLayoutTranslate |
            engine::editor::EditorProjectLayoutReset,
        .viewportMask =
            engine::editor::EditorProjectLayoutViewportScene,
        .translationSnap = {
            config.terrainTileSizeCm,
            config.terrainElevationStepCm,
            config.terrainTileSizeCm},
        .fineTranslationSnap = {
            config.terrainTileSizeCm,
            config.terrainElevationStepCm,
            config.terrainTileSizeCm},
        .sourceTranslation = config.defaultSourceAnchorCm,
        .sourceRotationDegrees = {0.0f, 0.0f, 0.0f},
        .sourceScale = {
            config.defaultBoardCellSizeWorld,
            config.defaultBoardCellSizeWorld,
            config.defaultBoardCellSizeWorld},
        .translation = layout.sourceAnchorCm,
        .rotationDegrees = {0.0f, layout.yawDegrees, 0.0f},
        .scale = {scale, scale, scale},
        .terrainGridOrigin = layout.terrainGridOrigin,
        .terrainGridExtent = layout.boardCells,
        .terrainElevationLevel = layout.terrainElevationLevel,
        .terrainGridBound = true,
        .useGridTranslationEditor = true,
        .boundsMinimum = {
            layout.sourceAnchorCm[0] - gameplayHalfWidth,
            layout.sourceAnchorCm[1],
            gameplayMinimumZ},
        .boundsMaximum = {
            layout.sourceAnchorCm[0] + gameplayHalfWidth,
            layout.sourceAnchorCm[1],
            gameplayMaximumZ},
        .suppressed = false,
        .hasOverride =
            layout.terrainGridOrigin != config.defaultTerrainGridOrigin ||
            layout.terrainElevationLevel != 0};

    view.terrainRegions[view.terrainRegionCount++] = {
        .label = "Board cells",
        .origin = layout.terrainGridOrigin,
        .extent = layout.boardCells,
        .outlineRgba = 0xffa62affu};
    if (layout.northBench) {
        view.terrainRegions[view.terrainRegionCount++] = {
            .label = "North bench cells",
            .origin =
                game::runtime::route1_environment::
                    northBenchTerrainGridOrigin(layout),
            .extent = {layout.benchSlots, 1u},
            .outlineRgba = 0x4cc4ffffu};
    }
    if (layout.southBench) {
        view.terrainRegions[view.terrainRegionCount++] = {
            .label = "South bench cells",
            .origin =
                game::runtime::route1_environment::
                    southBenchTerrainGridOrigin(layout),
            .extent = {layout.benchSlots, 1u},
            .outlineRgba = 0x4cc4ffffu};
    }
    return view;
}

} // namespace game::editor::hierarchy
