#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/runtime/shared/world/SharedBoardGridBatches.h"

#include <vector>

namespace game::runtime::shared_projected_scene::board_bench_geometry_cache {

void appendCachedBoardAndBench3D(const shared_board_grid::Config& cfg,
                                 std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
                                 std::vector<IRenderBackend::WorldTriangle>& world3DTriangles,
                                 std::vector<IRenderBackend::DebugQuad>& worldBackgroundQuads,
                                 std::vector<IRenderBackend::DebugLine>& lines);

} // namespace game::runtime::shared_projected_scene::board_bench_geometry_cache
