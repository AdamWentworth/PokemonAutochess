#include "game/editor/PokemonAutochessEditorHierarchy.h"

#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

bool test_editor_hierarchy_contract(std::string& outFail) {
    namespace hierarchy = game::editor::hierarchy;
    namespace route1 = game::runtime::lgpe_route1_runtime;

    if (hierarchy::objectCount(false, 4u, 3u) != 0u ||
        hierarchy::objectCount(true, 4u, 3u) != 8u) {
        outFail =
            "The editor hierarchy count should publish one board root followed by environment and preview objects only while the scene is ready.";
        return false;
    }

    struct ExpectedAddress {
        std::size_t hierarchyIndex;
        hierarchy::ObjectDomain domain;
        std::size_t domainIndex;
    };
    const std::array<ExpectedAddress, 6> expectedAddresses{{
        {0u, hierarchy::ObjectDomain::GameplayBoard, 0u},
        {1u, hierarchy::ObjectDomain::Environment, 0u},
        {2u, hierarchy::ObjectDomain::Environment, 1u},
        {3u, hierarchy::ObjectDomain::GameplayPreviewUnit, 0u},
        {4u, hierarchy::ObjectDomain::GameplayPreviewUnit, 1u},
        {5u, hierarchy::ObjectDomain::None, 0u},
    }};
    for (const auto& expected : expectedAddresses) {
        const auto address = hierarchy::resolveObjectAddress(
            true,
            2u,
            2u,
            expected.hierarchyIndex);
        if (address.domain != expected.domain ||
            address.index != expected.domainIndex) {
            outFail =
                "The editor hierarchy lost its stable board/environment/preview ordering.";
            return false;
        }
    }
    if (hierarchy::resolveObjectAddress(false, 2u, 2u, 0u).domain !=
        hierarchy::ObjectDomain::None) {
        outFail = "An unavailable scene should not resolve hierarchy indexes.";
        return false;
    }

    hierarchy::Selection selection;
    if (!selection.empty()) {
        outFail = "Editor hierarchy selection should begin empty.";
        return false;
    }
    std::string selectedId = "environment/tree-01";
    selection.select(selectedId);
    selectedId.clear();
    if (selection.empty() || !selection.matches("environment/tree-01") ||
        selection.id() != "environment/tree-01") {
        outFail = "Editor hierarchy selection should own its stable ID.";
        return false;
    }
    selection.select(static_cast<const char*>(nullptr));
    if (!selection.empty()) {
        outFail = "Selecting a null hierarchy ID should clear selection.";
        return false;
    }
    selection.select("gameplay/autochess-board");
    selection.clear();
    if (!selection.empty()) {
        outFail = "Clearing hierarchy selection should discard the selected ID.";
        return false;
    }

    const std::vector<std::pair<route1::LayoutObject, std::string_view>>
        classificationCases{
            {route1::LayoutObject{
                 .targetKind = "canonical_terrain_assembly"},
             "Source Terrain Assembly"},
            {route1::LayoutObject{
                 .targetKind = "canonical_mesh_group"},
             "Source Mesh Group"},
            {route1::LayoutObject{
                 .targetKind = "gameplay_board_ground_prototype"},
             "Gameplay Ground Prefab"},
            {route1::LayoutObject{
                 .targetKind = "canonical_tree_instance"},
             "Tree Prefab Placement"},
            {route1::LayoutObject{
                 .targetKind = "encounter_grass_record"},
             "Encounter Grass Prefab Placement"},
            {route1::LayoutObject{
                 .targetKind = "other_environment_kind"},
             "Environment Prefab Placement"},
            {route1::LayoutObject{
                 .targetKind = "canonical_mesh_group",
                 .authored = true},
             "Authored Prefab Instance"},
        };
    for (const auto& [object, expectedType] : classificationCases) {
        const auto view = hierarchy::environmentObjectView(object);
        if (!view.typeName || expectedType != view.typeName) {
            outFail =
                "Route 1 hierarchy object classification changed for target kind " +
                object.targetKind + ".";
            return false;
        }
    }

    route1::LayoutObject environmentObject{
        .stableId = "environment/tree-01",
        .displayName = "Tree 01",
        .targetKind = "canonical_tree_instance",
        .categoryPath = "Environment/Vegetation/Trees",
        .prefabAssetId = "route1/tree_oak",
        .sourceTranslationCm = {1.0f, 2.0f, 3.0f},
        .sourceRotationDegrees = {4.0f, 5.0f, 6.0f},
        .sourceScale = {0.5f, 0.6f, 0.7f},
        .translationCm = {7.0f, 8.0f, 9.0f},
        .rotationDegrees = {10.0f, 11.0f, 12.0f},
        .scale = {1.1f, 1.2f, 1.3f},
        .boundsMinimumCm = {-1.0f, -2.0f, -3.0f},
        .boundsMaximumCm = {1.0f, 2.0f, 3.0f},
        .suppressed = true,
        .hasOverride = true,
        .authored = false,
        .reason = "test_reason"};
    const auto environmentView =
        hierarchy::environmentObjectView(environmentObject);
    if (!environmentView.stableId ||
        std::string_view(environmentView.stableId) != environmentObject.stableId ||
        !environmentView.categoryPath ||
        std::string_view(environmentView.categoryPath) !=
            environmentObject.categoryPath ||
        environmentView.translation != environmentObject.translationCm ||
        environmentView.rotationDegrees != environmentObject.rotationDegrees ||
        environmentView.scale != environmentObject.scale ||
        environmentView.boundsMinimum != environmentObject.boundsMinimumCm ||
        environmentView.boundsMaximum != environmentObject.boundsMaximumCm ||
        !environmentView.suppressed || !environmentView.hasOverride) {
        outFail =
            "The environment hierarchy ABI view should preserve source-owned metadata and transforms.";
        return false;
    }

    route1::BoardLayoutTransform layout;
    const hierarchy::GameplayBoardViewConfig boardConfig{
        .stableId = "gameplay/autochess-board",
        .defaultSourceAnchorCm = {2100.0f, 0.0f, -1500.0f},
        .defaultTerrainGridOrigin = {17, -19},
        .defaultBoardCellSizeWorld = 1.0f,
        .terrainTileSizeCm = 100.0f,
        .terrainElevationStepCm = 50.0f};
    const auto boardView =
        hierarchy::gameplayBoardView(layout, boardConfig);
    const std::uint32_t expectedCapabilities =
        engine::editor::EditorProjectLayoutTranslate |
        engine::editor::EditorProjectLayoutReset;
    if (!boardView.stableId ||
        std::string_view(boardView.stableId) != boardConfig.stableId ||
        boardView.capabilities != expectedCapabilities ||
        boardView.viewportMask !=
            engine::editor::EditorProjectLayoutViewportScene ||
        !boardView.terrainGridBound || !boardView.useGridTranslationEditor ||
        boardView.terrainRegionCount != 3u || boardView.hasOverride ||
        boardView.boundsMinimum !=
            std::array<float, 3>{1700.0f, 0.0f, -2000.0f} ||
        boardView.boundsMaximum !=
            std::array<float, 3>{2500.0f, 0.0f, -1000.0f} ||
        boardView.translationSnap !=
            std::array<float, 3>{100.0f, 50.0f, 100.0f}) {
        outFail =
            "The gameplay-board hierarchy view should preserve grid editing, bench bounds, and default registration metadata.";
        return false;
    }
    layout.terrainGridOrigin = {18, -19};
    layout.northBench = false;
    layout.southBench = false;
    const auto movedBoardView =
        hierarchy::gameplayBoardView(layout, boardConfig);
    if (!movedBoardView.hasOverride ||
        movedBoardView.terrainRegionCount != 1u) {
        outFail =
            "The gameplay-board hierarchy view should report moved registration and omit disabled bench regions.";
        return false;
    }

    std::ifstream pluginSource("tools/PokemonAutochessEditorProject.cpp");
    const std::string pluginText{
        std::istreambuf_iterator<char>(pluginSource),
        std::istreambuf_iterator<char>()};
    if (pluginText.find("PokemonAutochessEditorHierarchy.h") ==
            std::string::npos ||
        pluginText.find("selectedLayoutObjectId_") != std::string::npos ||
        pluginText.find("Source Mesh Group") != std::string::npos ||
        pluginText.find("resolveObjectAddress") == std::string::npos) {
        outFail =
            "The editor plugin should delegate hierarchy ordering, classification, and selection ownership to the dedicated component.";
        return false;
    }

    return true;
}
