#include "game/editor/PokemonAutochessEditorSceneMutationSession.h"
#include "game/editor/PokemonAutochessEditorSceneMutations.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

bool test_editor_scene_mutations_contract(std::string& outFail) {
    namespace mutations = game::editor::scene_mutations;
    namespace mutation_session =
        game::editor::scene_mutation_session;
    namespace route1 =
        game::runtime::route1_environment;
    using Coordinate =
        engine::editor::EditorProjectTerrainTileCoordinate;
    using Request =
        engine::editor::EditorProjectTerrainTileEditRequest;
    using Stamp =
        engine::editor::EditorProjectTerrainTileStamp;

    const std::vector<route1::TerrainTileState> terrainTiles{
        route1::TerrainTileState{
            .gridX = 0,
            .gridZ = 0,
            .sourceElevationLevel = 2,
            .elevationLevel = 2,
            .sourceSurface = "light_lawn",
            .sourceShape = "ramp_north",
            .surface = "light_lawn",
            .shape = "flat",
            .sourceOccupied = true},
        route1::TerrainTileState{
            .gridX = 1,
            .gridZ = 0,
            .sourceElevationLevel = 4,
            .elevationLevel = 4,
            .sourceSurface = "dirt_path",
            .sourceShape = "flat",
            .surface = "dirt_path",
            .shape = "flat",
            .sourceOccupied = true}};
    const std::array<Coordinate, 2> duplicateCoordinates{{
        {.gridX = 0, .gridZ = 0},
        {.gridX = 0, .gridZ = 0}}};
    route1::BoardLayoutTransform current;
    mutations::TerrainTileEditResult result;
    std::string error;
    if (!mutations::buildTerrainTileEdit(
            Request{
                .coordinates = duplicateCoordinates.data(),
                .coordinateCount = duplicateCoordinates.size(),
                .operation = "raise"},
            terrainTiles,
            current,
            "route1/terrain_tileset",
            result,
            &error) ||
        result.affectedTileCount != 1u ||
        result.layout.authoredTerrainTiles.size() != 1u) {
        outFail =
            "Terrain mutation should deduplicate selected coordinates and author one tile.";
        return false;
    }
    const auto& raised = result.layout.authoredTerrainTiles.front();
    if (raised.elevationLevel != 3 ||
        raised.gridX != 0 || raised.gridZ != 0 ||
        raised.tileSetAssetId != "route1/terrain_tileset" ||
        raised.stableId !=
            route1::route1TerrainTileStableId(0, 0) ||
        raised.sourceReference.has_value()) {
        outFail =
            "Raise should preserve source-derived tile metadata, increment elevation, and leave source-reference mode.";
        return false;
    }

    const Coordinate origin{.gridX = 0, .gridZ = 0};
    mutations::TerrainTileEditResult platform;
    if (!mutations::buildTerrainTileEdit(
            Request{
                .coordinates = &origin,
                .coordinateCount = 1u,
                .operation = "platform_set",
                .surface = "dark_lawn",
                .shape = "source",
                .visualVariant = "auto",
                .targetElevationLevel = 5},
            terrainTiles,
            result.layout,
            "route1/terrain_tileset",
            platform,
            &error) ||
        platform.layout.authoredTerrainTiles.front().elevationLevel != 5 ||
        platform.layout.authoredTerrainTiles.front().surface !=
            "dark_lawn" ||
        platform.layout.authoredTerrainTiles.front().shape !=
            "ramp_north" ||
        platform.layout.authoredTerrainTiles.front().reason !=
            "terrain_platform_profiled") {
        outFail =
            "Platform mutation should apply its profile while resolving the source shape deterministically.";
        return false;
    }

    const std::array<Stamp, 2> stamps{{
        {
            .offsetGridX = 0,
            .relativeElevationLevel = 0,
            .absoluteElevationLevel = 7,
            .surface = "light_lawn",
            .shape = "flat",
            .visualVariant = "auto"},
        {
            .offsetGridX = 1,
            .relativeElevationLevel = 2,
            .absoluteElevationLevel = 9,
            .surface = "dirt_path",
            .shape = "flat",
            .visualVariant = "path_3",
            .sourceReference = {
                .gridX = 1,
                .gridZ = 0},
            .hasSourceReference = true}}};
    mutations::TerrainTileEditResult pasted;
    if (!mutations::buildTerrainTileEdit(
            Request{
                .coordinates = &origin,
                .coordinateCount = 1u,
                .operation = "paste_tiles_relative",
                .stampTiles = stamps.data(),
                .stampTileCount = stamps.size()},
            terrainTiles,
            current,
            "route1/terrain_tileset",
            pasted,
            &error) ||
        pasted.affectedTileCount != 2u ||
        pasted.layout.authoredTerrainTiles.size() != 2u ||
        pasted.layout.authoredTerrainTiles[0].elevationLevel != 2 ||
        pasted.layout.authoredTerrainTiles[1].elevationLevel != 4 ||
        pasted.layout.authoredTerrainTiles[1].visualVariant != "path_3" ||
        pasted.layout.authoredTerrainTiles[1].sourceReference !=
            std::optional<std::array<std::int32_t, 2>>{
                std::array<std::int32_t, 2>{1, 0}}) {
        outFail =
            "Relative terrain paste should retain internal tiers, variants, and validated source references.";
        return false;
    }

    mutations::TerrainTileEditResult restored;
    if (!mutations::buildTerrainTileEdit(
            Request{
                .coordinates = &origin,
                .coordinateCount = 1u,
                .operation = "restore_source"},
            terrainTiles,
            result.layout,
            "route1/terrain_tileset",
            restored,
            &error) ||
        !restored.layout.authoredTerrainTiles.empty()) {
        outFail =
            "Restore-source should remove the authored tile without mutating the input layout.";
        return false;
    }

    const std::array<Stamp, 2> duplicateStamps{{
        stamps[0], stamps[0]}};
    if (mutations::buildTerrainTileEdit(
            Request{
                .coordinates = &origin,
                .coordinateCount = 1u,
                .operation = "paste_tiles_exact",
                .stampTiles = duplicateStamps.data(),
                .stampTileCount = duplicateStamps.size()},
            terrainTiles,
            current,
            "route1/terrain_tileset",
            restored,
            &error) ||
        error !=
            "The copied terrain footprint contains an invalid tile state.") {
        outFail =
            "Terrain mutation should reject duplicate clipboard offsets before changing layout state.";
        return false;
    }

    route1::BoardLayoutTransform boardLayout;
    const auto movedBoard =
        mutations::boardRegistrationFromCenter(
            boardLayout,
            {2500.0f, 100.0f, -1000.0f},
            100.0f,
            50.0f);
    if (movedBoard.terrainGridOrigin !=
            std::array<std::int32_t, 2>{21, -14} ||
        movedBoard.terrainElevationLevel != 2 ||
        mutations::sameBoardRegistration(
            movedBoard, boardLayout)) {
        outFail =
            "Board registration should snap a requested source-space centre to terrain-grid origin and elevation.";
        return false;
    }
    const auto resetBoard =
        mutations::defaultBoardRegistration(
            movedBoard, {17, -19});
    if (resetBoard.terrainGridOrigin !=
            std::array<std::int32_t, 2>{17, -19} ||
        resetBoard.terrainElevationLevel != 0 ||
        !mutations::sameBoardRegistration(
            resetBoard, boardLayout)) {
        outFail =
            "Default board registration should restore the canonical terrain cell and height.";
        return false;
    }
    boardLayout.localLayoutDeltas.push_back({});
    boardLayout.objectMetadataOverrides.push_back({});
    boardLayout.authoredPrefabInstances.push_back({});
    boardLayout.authoredTerrainTiles.push_back({});
    boardLayout.declaredLocalDeltaCount = 1u;
    const auto importedBaseline =
        mutations::importedSceneBaseline(boardLayout);
    if (!importedBaseline.localLayoutDeltas.empty() ||
        !importedBaseline.objectMetadataOverrides.empty() ||
        !importedBaseline.authoredPrefabInstances.empty() ||
        !importedBaseline.authoredTerrainTiles.empty() ||
        importedBaseline.declaredLocalDeltaCount != 0u ||
        importedBaseline.terrainGridOrigin !=
            boardLayout.terrainGridOrigin) {
        outFail =
            "Imported-scene reset should clear authored collections while preserving board registration.";
        return false;
    }

    const std::array<float, 3> overlappingMinimum{
        2050.0f, 0.0f, -1550.0f};
    const std::array<float, 3> overlappingMaximum{
        2150.0f, 100.0f, -1450.0f};
    const std::vector<route1::LayoutObject> clearanceObjects{
        route1::LayoutObject{
            .stableId = "terrain/ramp",
            .targetKind = "canonical_terrain_assembly",
            .categoryPath = "Environment/Terrain/Ramps",
            .boundsMinimumCm = overlappingMinimum,
            .boundsMaximumCm = overlappingMaximum},
        route1::LayoutObject{
            .stableId = "terrain/flat",
            .targetKind = "canonical_terrain_assembly",
            .categoryPath = "Environment/Terrain/Assemblies",
            .boundsMinimumCm = overlappingMinimum,
            .boundsMaximumCm = overlappingMaximum},
        route1::LayoutObject{
            .stableId = "vegetation/tree",
            .targetKind = "canonical_tree_instance",
            .categoryPath = "Environment/Vegetation/Trees",
            .boundsMinimumCm = overlappingMinimum,
            .boundsMaximumCm = overlappingMaximum},
        route1::LayoutObject{
            .stableId = "vegetation/aggregate",
            .targetKind = "canonical_mesh_group",
            .categoryPath = "Environment/Vegetation/Aggregates",
            .boundsMinimumCm = overlappingMinimum,
            .boundsMaximumCm = overlappingMaximum},
        route1::LayoutObject{
            .stableId = "props/sign",
            .targetKind = "canonical_mesh_group",
            .categoryPath = "Environment/Props/Signs",
            .boundsMinimumCm = overlappingMinimum,
            .boundsMaximumCm = overlappingMaximum},
        route1::LayoutObject{
            .stableId = "board/ground-prototype",
            .targetKind = "canonical_mesh_group",
            .categoryPath = "Environment/Props",
            .boundsMinimumCm = overlappingMinimum,
            .boundsMaximumCm = overlappingMaximum}};
    const mutations::BoardClearanceConfig clearanceConfig{
        .boardCellSizeWorld = 1.0f,
        .terrainTileSizeCm = 100.0f,
        .terrainElevationStepCm = 50.0f,
        .groundPrototypeStableId =
            "board/ground-prototype",
        .groundPrefabAssetId = "route1/board-ground",
        .groundInstanceStableId = "board/ground-instance",
        .terrainTileSetAssetId = "route1/terrain_tileset"};
    mutations::BoardClearancePlan clearancePlan;
    const game::editor::commands::BoardClearanceRequest
        clearanceRequest{
            .paddingCells = 0.0f,
            .clearTerrain = true,
            .clearVegetation = true,
            .clearObjects = true,
            .retainRamps = true,
            .addGroundInfill = false};
    if (!mutations::buildBoardClearancePlan(
            clearanceRequest,
            route1::BoardLayoutTransform{},
            clearanceObjects,
            {},
            clearanceConfig,
            clearancePlan,
            &error) ||
        clearancePlan.result.suppressedTerrainCount != 1u ||
        clearancePlan.result.suppressedVegetationCount != 1u ||
        clearancePlan.result.suppressedObjectCount != 1u ||
        clearancePlan.result.retainedRampCount != 1u ||
        clearancePlan.result.skippedUnsafeAggregateCount != 1u ||
        clearancePlan.suppressStableIds !=
            std::vector<std::string>{
                "terrain/flat",
                "vegetation/tree",
                "props/sign"}) {
        outFail =
            "Board-clearance planning should classify terrain, exact vegetation, unsafe aggregates, ramps, props, and protected ground independently.";
        return false;
    }

    const std::vector<route1::TerrainTileState> infillTerrain{
        route1::TerrainTileState{
            .gridX = 21,
            .gridZ = -15,
            .elevationLevel = 3,
            .surface = "dirt_path",
            .shape = "ramp_east",
            .sourceOccupied = true}};
    auto infillRequest = clearanceRequest;
    infillRequest.addGroundInfill = true;
    route1::BoardLayoutTransform raisedBoard;
    raisedBoard.terrainElevationLevel = 2;
    mutations::BoardClearancePlan infillPlan;
    if (!mutations::buildBoardClearancePlan(
            infillRequest,
            raisedBoard,
            clearanceObjects,
            infillTerrain,
            clearanceConfig,
            infillPlan,
            &error) ||
        !infillPlan.result.groundInfillCreated ||
        infillPlan.groundInfillTiles.size() != 1u ||
        infillPlan.groundInfillTiles.front().gridX != 21 ||
        infillPlan.groundInfillTiles.front().gridZ != -15 ||
        infillPlan.groundInfillTiles.front().elevationLevel != 2 ||
        infillPlan.groundInfillTiles.front().surface !=
            "light_lawn" ||
        infillPlan.result.suppressedTerrainCount != 0u) {
        outFail =
            "Board-clearance infill should replace overlapping source terrain with flat lawn at the board elevation without suppressing its connected assembly.";
        return false;
    }
    mutations::BoardClearancePlan missingInfill;
    if (mutations::buildBoardClearancePlan(
            infillRequest,
            raisedBoard,
            clearanceObjects,
            {},
            clearanceConfig,
            missingInfill,
            &error) ||
        error !=
            "The autochess board footprint did not overlap the Route 1 terrain grid.") {
        outFail =
            "Ground-infill planning should reject a board footprint with no editable Route 1 terrain cells before runtime mutation.";
        return false;
    }

    game::runtime::route1_environment::RuntimeEnvironment
        unmountedEnvironment;
    game::editor::persistence::Store unmountedPersistence;
    const std::filesystem::path noAuthoredScene;
    mutation_session::Session unmountedSession(
        unmountedEnvironment,
        unmountedPersistence,
        false,
        noAuthoredScene);
    mutation_session::Session::FailureStage failureStage =
        mutation_session::Session::FailureStage::None;
    if (unmountedSession.applyAuthoredLayout(
            route1::BoardLayoutTransform{},
            route1::BoardLayoutTransform{},
            &error,
            &failureStage) ||
        failureStage !=
            mutation_session::Session::FailureStage::Apply) {
        outFail =
            "The mutation session should identify runtime-apply failure before attempting persistence.";
        return false;
    }

    std::ifstream pluginSource(
        "tools/PokemonAutochessEditorProject.cpp");
    const std::string pluginText{
        std::istreambuf_iterator<char>(pluginSource),
        std::istreambuf_iterator<char>()};
    if (pluginText.find(
            "PokemonAutochessEditorSceneMutations.h") ==
            std::string::npos ||
        pluginText.find(
            "PokemonAutochessEditorSceneMutationSession.h") ==
            std::string::npos ||
        pluginText.find("buildTerrainTileEdit") ==
            std::string::npos ||
        pluginText.find("validVariantForSurface") !=
            std::string::npos ||
        pluginText.find("pasteTilesRelative") !=
            std::string::npos ||
        pluginText.find("suppressedTerrainCount") !=
            std::string::npos ||
        pluginText.find("environment_.deleteLayoutObject") !=
            std::string::npos ||
        pluginText.find("saveLayoutManifest") !=
            std::string::npos) {
        outFail =
            "The editor plugin should delegate deterministic terrain/board planning and committed object mutation transactions to the scene-mutation component.";
        return false;
    }

    return true;
}
