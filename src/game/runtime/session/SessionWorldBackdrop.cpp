#include "game/runtime/session/SessionWorldBackdrop.h"

#include "game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.h"

#include <chrono>

namespace game::runtime::session_world_backdrop {

namespace {

using RenderBuildClock = std::chrono::steady_clock;

session_render_scratch::ProjectedBackdropCacheKey makeProjectedBackdropKey(
    const ProjectedBackdropArgs& args) {
    session_render_scratch::ProjectedBackdropCacheKey key{};
    key.supportsWorldTriangles3D = args.supportsWorldTriangles3D;
    key.rows = args.rows;
    key.cols = args.cols;
    key.benchSlots = args.benchSlots;
    key.worldCellSize = args.worldCellSize;
    key.boardMinX = args.boardMinX;
    key.boardMinZ = args.boardMinZ;
    key.boardMaxX = args.boardMaxX;
    key.boardMaxZ = args.boardMaxZ;
    key.boardX = args.boardX;
    key.boardY = args.boardY;
    key.boardW = args.boardW;
    key.boardH = args.boardH;
    key.cellW = args.cellW;
    key.cellH = args.cellH;
    key.line = args.line;
    return key;
}

shared_board_grid::Config makeBoardGridConfig(const ProjectedBackdropArgs& args) {
    return shared_projected_scene::makeBoardGridConfig(
        args.supportsWorldTriangles3D,
        args.rows,
        args.cols,
        args.benchSlots,
        args.worldCellSize,
        args.boardMinX,
        args.boardMinZ,
        args.boardMaxX,
        args.boardMaxZ,
        args.boardX,
        args.boardY,
        args.boardW,
        args.boardH,
        args.cellW,
        args.cellH,
        args.line);
}

void appendBackdropGeometry(const ProjectedBackdropArgs& args,
                            shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
                            session_render_scratch::RenderScratch& scratch) {
    const shared_board_grid::Config boardGridCfg = makeBoardGridConfig(args);
    shared_projected_scene::appendBoardAndBench(
        boardGridCfg,
        scratch.worldTriangles,
        scratch.world3DTriangles,
        scratch.worldBackgroundQuads,
        scratch.lines,
        projectedDebug);
}

} // namespace

float composeProjectedBackdrop(const ProjectedBackdropArgs& args,
                               shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
                               session_render_scratch::RenderScratch& scratch) {
    const auto composeStart = RenderBuildClock::now();

    if (args.supportsWorldTriangles3D) {
        const session_render_scratch::ProjectedBackdropCacheKey projectedBackdropKey =
            makeProjectedBackdropKey(args);
        if (!scratch.projectedBackdropValid ||
            !(scratch.projectedBackdropKey == projectedBackdropKey)) {
            scratch.worldBackgroundQuads.clear();
            scratch.worldTriangles.clear();
            scratch.world3DTriangles.clear();
            scratch.lines.clear();

            appendBackdropGeometry(args, projectedDebug, scratch);

            scratch.projectedBackdropValid = true;
            scratch.projectedBackdropKey = projectedBackdropKey;
            scratch.projectedBackdropWorldBackgroundQuadsCount =
                scratch.worldBackgroundQuads.size();
            scratch.projectedBackdropWorldTrianglesCount = scratch.worldTriangles.size();
            scratch.projectedBackdropWorld3DTrianglesCount = scratch.world3DTriangles.size();
            scratch.projectedBackdropLinesCount = scratch.lines.size();
        } else {
            scratch.worldBackgroundQuads.resize(
                scratch.projectedBackdropWorldBackgroundQuadsCount);
            scratch.worldTriangles.resize(scratch.projectedBackdropWorldTrianglesCount);
            scratch.world3DTriangles.resize(
                scratch.projectedBackdropWorld3DTrianglesCount);
            scratch.lines.resize(scratch.projectedBackdropLinesCount);
        }
    } else {
        session_render_scratch::invalidateProjectedBackdrop(scratch);
        appendBackdropGeometry(args, projectedDebug, scratch);
    }

    return static_cast<float>(
        std::chrono::duration<double, std::milli>(
            RenderBuildClock::now() - composeStart).count());
}

} // namespace game::runtime::session_world_backdrop
