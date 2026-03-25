#include "game/runtime/session/SessionWorldBackdrop.h"
#include "engine/core/Paths.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"

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
        args.theme = game::runtime::session_world_backdrop::ArenaBackdropTheme::Route1OpenRoad;
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
        const std::size_t cachedIndexedBatchCount = scratch.worldIndexedBatches.size();
        const std::size_t cachedLineCount = scratch.lines.size();
        const std::string cachedIndexedGeometryKey =
            cachedIndexedBatchCount > 0u ? scratch.worldIndexedBatches.front().geometryCacheKey
                                         : std::string{};
        if (!scratch.projectedBackdropValid ||
            scratch.projectedBackdropWorldBackgroundQuadsCount != cachedBackgroundCount ||
            scratch.projectedBackdropWorldTrianglesCount != cachedTriangleCount ||
            scratch.projectedBackdropWorld3DTrianglesCount != cachedWorld3DCount ||
            scratch.projectedBackdropWorldIndexedBatchesCount != cachedIndexedBatchCount ||
            scratch.projectedBackdropLinesCount != cachedLineCount ||
            firstComposeMs < 0.0f) {
            outFail = "SessionWorldBackdrop should populate and cache projected backdrop geometry.";
            return false;
        }

        scratch.worldBackgroundQuads.push_back({});
        scratch.worldTriangles.push_back({});
        scratch.world3DTriangles.push_back({});
        scratch.worldIndexedBatches.push_back({});
        scratch.lines.push_back({});
        const float secondComposeMs = composeProjectedBackdrop(args, projectedDebug, scratch);
        if (scratch.worldBackgroundQuads.size() != cachedBackgroundCount ||
            scratch.worldTriangles.size() != cachedTriangleCount ||
            scratch.world3DTriangles.size() != cachedWorld3DCount ||
            scratch.worldIndexedBatches.size() != cachedIndexedBatchCount ||
            (cachedIndexedBatchCount > 0u &&
             scratch.worldIndexedBatches.front().geometryCacheKey != cachedIndexedGeometryKey) ||
            scratch.lines.size() != cachedLineCount ||
            secondComposeMs < 0.0f) {
            outFail = "SessionWorldBackdrop should reuse cached projected backdrop sizes for unchanged keys.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        auto projectedDebug = makeProjectedDebug(true, scratch);
        ProjectedBackdropArgs args = makeArgs(true);
        composeProjectedBackdrop(args, projectedDebug, scratch);
        const std::size_t route1World3DCount = scratch.world3DTriangles.size();

        args.theme = game::runtime::session_world_backdrop::ArenaBackdropTheme::ViridianForestShrine;
        composeProjectedBackdrop(args, projectedDebug, scratch);
        if (!scratch.projectedBackdropValid ||
            scratch.projectedBackdropKey.arenaBackdropTheme !=
                static_cast<int>(game::runtime::session_world_backdrop::ArenaBackdropTheme::ViridianForestShrine) ||
            scratch.world3DTriangles.size() == route1World3DCount) {
            outFail = "SessionWorldBackdrop should rebuild and retheme cached backdrop geometry when the route theme changes.";
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
            scratch.projectedBackdropWorldIndexedBatchesCount != 0u ||
            scratch.projectedBackdropLinesCount != 0u) {
            outFail = "SessionWorldBackdrop should build noncached projected backdrop geometry when 3D world triangles are unavailable.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        auto projectedDebug = makeProjectedDebug(true, scratch);
        ProjectedBackdropArgs args = makeArgs(true);
        args.supportsWorldIndexedMeshes = true;
        args.graphicsQuality = 3;

        game::runtime::render_model::MeshData treeMesh;
        std::string treeError;
        const std::string treeModelPath =
            engine::paths::asset("models/environment/route_evergreen_tree.glb");
        const bool treeLoaded =
            game::runtime::render_model::loadMeshFromCache(treeModelPath, treeMesh, &treeError);
        if (!treeLoaded) {
            outFail = "Failed to load backdrop tree asset: " + treeError;
            return false;
        }

        args.ensureBackendMeshLoaded =
            [&](const std::string& modelPath)
                -> game::runtime::render_model::MeshData* {
                if (modelPath == "assets/models/environment/route_evergreen_tree.glb") {
                    return &treeMesh;
                }
                return nullptr;
            };
        composeProjectedBackdrop(args, projectedDebug, scratch);
        if (scratch.worldIndexedBatches.empty() ||
            scratch.projectedBackdropWorldIndexedBatchesCount == 0u) {
            outFail =
                "SessionWorldBackdrop should append cached indexed batches for authored route props.";
            return false;
        }

        const std::size_t treeTriangleCount = treeMesh.indices.size() / 3u;
        const std::size_t authoredBackdropTriangles =
            scratch.worldIndexedBatches.size() * treeTriangleCount;
        if (authoredBackdropTriangles >
            game::runtime::session_world_backdrop::
                authoredTreeTriangleBudgetForGraphicsQuality(args.graphicsQuality)) {
            outFail =
                "SessionWorldBackdrop should keep authored tree triangles within the quality budget.";
            return false;
        }

        RenderScratch lowScratch;
        auto lowProjectedDebug = makeProjectedDebug(true, lowScratch);
        args.graphicsQuality = 0;
        composeProjectedBackdrop(args, lowProjectedDebug, lowScratch);
        const std::size_t lowBudget =
            game::runtime::session_world_backdrop::
                authoredTreeTriangleBudgetForGraphicsQuality(args.graphicsQuality);
        const std::size_t lowAuthoredBackdropTriangles =
            lowScratch.worldIndexedBatches.size() * treeTriangleCount;
        if (lowAuthoredBackdropTriangles > lowBudget) {
            outFail =
                "SessionWorldBackdrop low-quality authored tree triangles exceeded the quality budget.";
            return false;
        }
        if (lowBudget >= treeTriangleCount &&
            lowScratch.worldIndexedBatches.empty()) {
            outFail =
                "SessionWorldBackdrop should keep authored trees enabled at low quality when the active asset fits the budget.";
            return false;
        }
    }

    return true;
}
