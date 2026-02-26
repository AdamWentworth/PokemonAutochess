#pragma once

#include "engine/render/IRenderBackend.h"

#include <functional>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_board_grid {

struct Config {
    bool supportsWorldTriangles3D = false;
    int rows = 0;
    int cols = 0;
    int benchSlots = 0;
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

