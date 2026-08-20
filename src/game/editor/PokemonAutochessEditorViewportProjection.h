#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace game::editor::viewport_projection {

struct Context {
    // Matrices use the column-major layout published by the editor ABI.
    const float* viewProjectionMatrix4x4 = nullptr;
    // A null source-to-world matrix treats source points as world points.
    const float* sourceToWorldMatrix4x4 = nullptr;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
};

bool projectPoint(
    const Context& context,
    const std::array<float, 3>& sourcePoint,
    std::array<float, 2>& outViewportPoint) noexcept;

struct TransformInput {
    std::array<float, 3> sourcePosition{};
    float sourceAxisLength = 1.0f;
    std::array<float, 6> fallbackAxisDirections{};
    std::array<float, 3> fallbackSourceUnitsPerPixel{
        1.0f, 1.0f, 1.0f};
};

struct TransformProjection {
    std::array<float, 2> viewportPosition{};
    std::array<float, 6> viewportAxisDirections{};
    std::array<float, 3> viewportSourceUnitsPerPixel{
        1.0f, 1.0f, 1.0f};
    bool visible = false;
};

TransformProjection projectTransform(
    const Context& context,
    const TransformInput& input) noexcept;

struct TerrainTileInput {
    std::int32_t gridX = 0;
    std::int32_t gridZ = 0;
    std::int32_t elevationLevel = 0;
    std::string_view shape;
    float tileSize = 100.0f;
    float elevationStep = 50.0f;
    float surfaceLift = 1.0f;
};

struct TerrainTileProjection {
    std::array<float, 8> viewportCorners{};
    std::array<float, 8> viewportFlatCorners{};
    std::array<float, 8> viewportLevelStep{};
    bool visible = false;
};

TerrainTileProjection projectTerrainTile(
    const Context& context,
    const TerrainTileInput& input) noexcept;

} // namespace game::editor::viewport_projection
