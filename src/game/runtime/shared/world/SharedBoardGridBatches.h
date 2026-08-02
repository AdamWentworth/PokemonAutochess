#pragma once

#include "engine/render/IRenderBackend.h"

#include <functional>
#include <array>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_board_grid {

struct VisualTheme {
    float boardSurfaceY = 0.006f;
    float gridY = 0.0090f;
    float gridHalfWidthMin = 0.0035f;
    float gridHalfWidthScale = 0.0180f;
    float benchGapMin = 0.0f;
    float benchGapScale = 1.0f;
    std::array<float, 4> boardCellDark{0.07f, 0.08f, 0.09f, 0.32f};
    std::array<float, 4> boardCellLight{0.10f, 0.11f, 0.12f, 0.26f};
    std::array<float, 4> benchCellDark{0.075f, 0.085f, 0.095f, 0.28f};
    std::array<float, 4> benchCellLight{0.105f, 0.115f, 0.125f, 0.24f};
    std::array<float, 4> gridLine{0.82f, 0.83f, 0.85f, 0.94f};
    std::array<float, 4> fallbackBoardBackground{0.06f, 0.07f, 0.08f, 0.92f};
    std::array<float, 4> fallbackBoardCellDark{0.09f, 0.14f, 0.19f, 0.34f};
    std::array<float, 4> fallbackBoardCellLight{0.14f, 0.19f, 0.25f, 0.26f};
    std::array<float, 4> fallbackGridLine{0.26f, 0.38f, 0.47f, 0.96f};
};

const VisualTheme& defaultVisualTheme();

struct Config {
    bool supportsWorldTriangles3D = false;
    int rows = 0;
    int cols = 0;
    int benchSlots = 0;
    int benchGapCells = 0;
    float worldCellSize = 1.0f;
    float boardMinX = 0.0f;
    float boardMinZ = 0.0f;
    float boardMaxX = 0.0f;
    float boardMaxZ = 0.0f;
    float boardX = 0.0f;
    float boardY = 0.0f;
    float boardW = 0.0f;
    float boardH = 0.0f;
    float cellW = 0.0f;
    float cellH = 0.0f;
    float line = 1.0f;
    const VisualTheme* visualTheme = nullptr;
};

using AppendWorldQuadFn = std::function<void(
    const glm::vec3&,
    const glm::vec3&,
    const glm::vec3&,
    const glm::vec3&,
    float,
    float,
    float,
    float)>;

using AppendProjectedQuadFn = AppendWorldQuadFn;

using AppendProjectedLineFn = std::function<void(
    const glm::vec3&,
    const glm::vec3&,
    float,
    float,
    float,
    float,
    float)>;

void appendBoardAndBench(
    const Config& cfg,
    std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
    std::vector<IRenderBackend::WorldTriangle>& world3DTriangles,
    std::vector<IRenderBackend::DebugQuad>& worldBackgroundQuads,
    std::vector<IRenderBackend::DebugLine>& lines,
    const AppendWorldQuadFn& appendWorldQuad,
    const AppendProjectedQuadFn& appendProjectedQuad,
    const AppendProjectedLineFn& appendProjectedLine);

} // namespace game::runtime::shared_board_grid
