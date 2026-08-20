#include "game/editor/PokemonAutochessEditorSceneMutations.h"

#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

bool test_editor_scene_mutations_contract(std::string& outFail) {
    namespace mutations = game::editor::scene_mutations;
    namespace route1 =
        game::runtime::lgpe_route1_runtime;
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

    std::ifstream pluginSource(
        "tools/PokemonAutochessEditorProject.cpp");
    const std::string pluginText{
        std::istreambuf_iterator<char>(pluginSource),
        std::istreambuf_iterator<char>()};
    if (pluginText.find(
            "PokemonAutochessEditorSceneMutations.h") ==
            std::string::npos ||
        pluginText.find("buildTerrainTileEdit") ==
            std::string::npos ||
        pluginText.find("validVariantForSurface") !=
            std::string::npos ||
        pluginText.find("pasteTilesRelative") !=
            std::string::npos) {
        outFail =
            "The editor plugin should delegate deterministic terrain request validation and layout mutation to the scene-mutation component.";
        return false;
    }

    return true;
}
