#include "game/runtime/session/SessionWorldBackdrop.h"

#include <string>

#include <glm/glm.hpp>

bool test_session_world_backdrop_contract(std::string& outFail) {
    using game::runtime::session_render_scratch::RenderScratch;
    using game::runtime::session_world_backdrop::ProjectedBackdropArgs;
    using game::runtime::session_world_backdrop::composeProjectedBackdrop;

    auto makeProjectedDebug = [](bool supportsWorldTriangles3D,
                                 RenderScratch& scratch) {
        const glm::mat4 view(1.0f);
        const glm::mat4 proj(1.0f);
        const glm::vec4 viewport(0.0f, 0.0f, 1280.0f, 720.0f);
        return game::runtime::shared_projected_debug::ProjectedDebugVfxBuilder(
            supportsWorldTriangles3D,
            view,
            proj,
            720,
            viewport,
            scratch.worldTriangles,
            scratch.world3DTriangles,
            scratch.lines);
    };

    auto makeArgs = [](bool supportsWorldTriangles3D) {
        ProjectedBackdropArgs args;
        args.supportsWorldTriangles3D = supportsWorldTriangles3D;
        args.rows = 4;
        args.cols = 8;
        args.benchSlots = 8;
        args.worldCellSize = 1.0f;
        args.boardMinX = -4.0f;
        args.boardMinZ = -2.0f;
        args.boardMaxX = 4.0f;
        args.boardMaxZ = 2.0f;
        args.boardX = 100.0f;
        args.boardY = 80.0f;
        args.boardW = 800.0f;
        args.boardH = 400.0f;
        args.cellW = 100.0f;
        args.cellH = 100.0f;
        args.line = 2.0f;
        return args;
    };

    {
        RenderScratch scratch;
        auto projectedDebug = makeProjectedDebug(true, scratch);
        const ProjectedBackdropArgs args = makeArgs(true);
        const float firstComposeMs = composeProjectedBackdrop(args, projectedDebug, scratch);
        const std::size_t cachedBackgroundCount = scratch.worldBackgroundQuads.size();
        const std::size_t cachedTriangleCount = scratch.worldTriangles.size();
        const std::size_t cachedWorld3DCount = scratch.world3DTriangles.size();
        const std::size_t cachedLineCount = scratch.lines.size();
        if (!scratch.projectedBackdropValid ||
            scratch.projectedBackdropWorldBackgroundQuadsCount != cachedBackgroundCount ||
            scratch.projectedBackdropWorldTrianglesCount != cachedTriangleCount ||
            scratch.projectedBackdropWorld3DTrianglesCount != cachedWorld3DCount ||
            scratch.projectedBackdropLinesCount != cachedLineCount ||
            firstComposeMs < 0.0f) {
            outFail = "SessionWorldBackdrop should populate and cache projected backdrop geometry.";
            return false;
        }

        scratch.worldBackgroundQuads.push_back({});
        scratch.worldTriangles.push_back({});
        scratch.world3DTriangles.push_back({});
        scratch.lines.push_back({});
        const float secondComposeMs = composeProjectedBackdrop(args, projectedDebug, scratch);
        if (scratch.worldBackgroundQuads.size() != cachedBackgroundCount ||
            scratch.worldTriangles.size() != cachedTriangleCount ||
            scratch.world3DTriangles.size() != cachedWorld3DCount ||
            scratch.lines.size() != cachedLineCount ||
            secondComposeMs < 0.0f) {
            outFail = "SessionWorldBackdrop should reuse cached projected backdrop sizes for unchanged keys.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        scratch.projectedBackdropValid = true;
        scratch.projectedBackdropWorldBackgroundQuadsCount = 3u;
        auto projectedDebug = makeProjectedDebug(false, scratch);
        const ProjectedBackdropArgs args = makeArgs(false);
        composeProjectedBackdrop(args, projectedDebug, scratch);
        if (scratch.projectedBackdropValid ||
            scratch.projectedBackdropWorldBackgroundQuadsCount != 0u ||
            scratch.projectedBackdropWorldTrianglesCount != 0u ||
            scratch.projectedBackdropWorld3DTrianglesCount != 0u ||
            scratch.projectedBackdropLinesCount != 0u) {
            outFail = "SessionWorldBackdrop should build noncached projected backdrop geometry when 3D world triangles are unavailable.";
            return false;
        }
    }

    return true;
}
