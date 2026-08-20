#include "game/editor/PokemonAutochessEditorViewportProjection.h"

#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

namespace {

bool nearlyEqual(float left, float right) {
    return std::abs(left - right) <= 0.0001f;
}

constexpr std::array<float, 16> kIdentityMatrix{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f};

} // namespace

bool test_editor_viewport_projection_contract(
    std::string& outFail) {
    namespace projection =
        game::editor::viewport_projection;

    const projection::Context worldContext{
        .viewProjectionMatrix4x4 = kIdentityMatrix.data(),
        .surfaceWidth = 200,
        .surfaceHeight = 100};
    std::array<float, 2> viewportPoint{};
    if (!projection::projectPoint(
            worldContext,
            {0.0f, 0.0f, 0.0f},
            viewportPoint) ||
        !nearlyEqual(viewportPoint[0], 100.0f) ||
        !nearlyEqual(viewportPoint[1], 50.0f)) {
        outFail =
            "Viewport projection should map the clip-space origin to the surface centre.";
        return false;
    }
    if (projection::projectPoint(
            projection::Context{},
            {0.0f, 0.0f, 0.0f},
            viewportPoint) ||
        projection::projectPoint(
            worldContext,
            {0.0f, 0.0f, 2.0f},
            viewportPoint)) {
        outFail =
            "Viewport projection should reject incomplete surfaces and points outside the clip-depth range.";
        return false;
    }

    auto translatedSource = kIdentityMatrix;
    translatedSource[12] = 0.25f;
    translatedSource[13] = -0.5f;
    const projection::Context sourceContext{
        .viewProjectionMatrix4x4 = kIdentityMatrix.data(),
        .sourceToWorldMatrix4x4 = translatedSource.data(),
        .surfaceWidth = 200,
        .surfaceHeight = 100};
    if (!projection::projectPoint(
            sourceContext,
            {0.0f, 0.0f, 0.0f},
            viewportPoint) ||
        !nearlyEqual(viewportPoint[0], 125.0f) ||
        !nearlyEqual(viewportPoint[1], 75.0f)) {
        outFail =
            "Viewport projection should apply the Route 1 source-to-world transform before the camera transform.";
        return false;
    }

    const auto transform = projection::projectTransform(
        worldContext,
        projection::TransformInput{
            .sourcePosition = {0.0f, 0.0f, 0.0f},
            .sourceAxisLength = 0.5f,
            .fallbackAxisDirections = {
                -1.0f, -1.0f,
                -2.0f, -2.0f,
                0.70710678f, 0.70710678f},
            .fallbackSourceUnitsPerPixel = {
                1.0f, 2.0f, 3.0f}});
    if (!transform.visible ||
        transform.viewportPosition !=
            std::array<float, 2>{100.0f, 50.0f} ||
        !nearlyEqual(transform.viewportAxisDirections[0], 1.0f) ||
        !nearlyEqual(transform.viewportAxisDirections[1], 0.0f) ||
        !nearlyEqual(transform.viewportAxisDirections[2], 0.0f) ||
        !nearlyEqual(transform.viewportAxisDirections[3], -1.0f) ||
        !nearlyEqual(
            transform.viewportAxisDirections[4],
            0.70710678f) ||
        !nearlyEqual(
            transform.viewportSourceUnitsPerPixel[0],
            0.01f) ||
        !nearlyEqual(
            transform.viewportSourceUnitsPerPixel[1],
            0.02f) ||
        !nearlyEqual(
            transform.viewportSourceUnitsPerPixel[2],
            3.0f)) {
        outFail =
            "Transform projection should derive screen-space X/Y axes and preserve fallbacks for a view-collapsed axis.";
        return false;
    }

    const projection::Context terrainContext{
        .viewProjectionMatrix4x4 = kIdentityMatrix.data(),
        .surfaceWidth = 100,
        .surfaceHeight = 100};
    const auto ramp = projection::projectTerrainTile(
        terrainContext,
        projection::TerrainTileInput{
            .shape = "ramp_east",
            .tileSize = 1.0f,
            .elevationStep = 0.5f,
            .surfaceLift = 0.0f});
    if (!ramp.visible ||
        ramp.viewportCorners != std::array<float, 8>{
            50.0f, 50.0f,
            100.0f, 25.0f,
            100.0f, 25.0f,
            50.0f, 50.0f} ||
        ramp.viewportFlatCorners != std::array<float, 8>{
            50.0f, 50.0f,
            100.0f, 50.0f,
            100.0f, 50.0f,
            50.0f, 50.0f}) {
        outFail =
            "Terrain projection should raise only the direction-facing ramp corners while retaining a flat editing footprint.";
        return false;
    }
    for (std::size_t corner = 0u; corner < 4u; ++corner) {
        if (!nearlyEqual(
                ramp.viewportLevelStep[corner * 2u],
                0.0f) ||
            !nearlyEqual(
                ramp.viewportLevelStep[corner * 2u + 1u],
                -25.0f)) {
            outFail =
                "Terrain projection should publish one consistent elevation-level drag step per corner.";
            return false;
        }
    }
    const auto clippedTerrain =
        projection::projectTerrainTile(
            terrainContext,
            projection::TerrainTileInput{
                .gridZ = 2,
                .tileSize = 1.0f,
                .surfaceLift = 0.0f});
    if (clippedTerrain.visible) {
        outFail =
            "Terrain editing should stay unavailable when any tile corner cannot be projected.";
        return false;
    }

    std::ifstream pluginSource(
        "tools/PokemonAutochessEditorProject.cpp");
    const std::string pluginText{
        std::istreambuf_iterator<char>(pluginSource),
        std::istreambuf_iterator<char>()};
    if (pluginText.find(
            "PokemonAutochessEditorViewportProjection.h") ==
            std::string::npos ||
        pluginText.find("projectEditorPoint") !=
            std::string::npos ||
        pluginText.find("projectTerrainTile") ==
            std::string::npos ||
        pluginText.find("projectTransform") ==
            std::string::npos) {
        outFail =
            "The editor plugin should delegate point, transform-gizmo, and terrain-corner projection to the dedicated component.";
        return false;
    }

    return true;
}
