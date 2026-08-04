#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"

#include "game/runtime/shared/projected/world_scene/SharedProjectedBoardBenchGeometryCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireCoordinator.h"

#include <algorithm>
#include <utility>

namespace game::runtime::shared_projected_scene {

ModelDepthBuffers acquireModelDepthBuffers(std::size_t reserveCount) {
    static thread_local std::vector<DepthTri> modelDepthTris;
    static thread_local std::vector<DepthWorldTri> modelDepthWorldTris;
    modelDepthTris.clear();
    modelDepthWorldTris.clear();
    if (modelDepthTris.capacity() < reserveCount) modelDepthTris.reserve(reserveCount);
    if (modelDepthWorldTris.capacity() < reserveCount) modelDepthWorldTris.reserve(reserveCount);
    return {modelDepthTris, modelDepthWorldTris};
}

shared_board_grid::Config makeBoardGridConfig(bool supportsWorldTriangles3D,
                                              int rows,
                                              int cols,
                                              int benchSlots,
                                              int benchGapCells,
                                              float worldCellSize,
                                              float boardMinX,
                                              float boardMinZ,
                                              float boardMaxX,
                                              float boardMaxZ,
                                              float boardX,
                                              float boardY,
                                              float boardW,
                                              float boardH,
                                              float cellW,
                                              float cellH,
                                              float line) {
    shared_board_grid::Config cfg;
    cfg.supportsWorldTriangles3D = supportsWorldTriangles3D;
    cfg.rows = rows;
    cfg.cols = cols;
    cfg.benchSlots = benchSlots;
    cfg.benchGapCells = benchGapCells;
    cfg.worldCellSize = worldCellSize;
    cfg.boardMinX = boardMinX;
    cfg.boardMinZ = boardMinZ;
    cfg.boardMaxX = boardMaxX;
    cfg.boardMaxZ = boardMaxZ;
    cfg.boardX = boardX;
    cfg.boardY = boardY;
    cfg.boardW = boardW;
    cfg.boardH = boardH;
    cfg.cellW = cellW;
    cfg.cellH = cellH;
    cfg.line = line;
    cfg.visualTheme = &shared_board_grid::defaultVisualTheme();
    return cfg;
}

void flushModelDepthBuffers(std::vector<DepthTri>& modelDepthTris,
                            std::vector<DepthWorldTri>& modelDepthWorldTris,
                            std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
                            std::vector<IRenderBackend::WorldTriangle>& world3DTriangles) {
    if (!modelDepthWorldTris.empty()) {
        std::sort(
            modelDepthWorldTris.begin(),
            modelDepthWorldTris.end(),
            [](const DepthWorldTri& lhs, const DepthWorldTri& rhs) { return lhs.depth > rhs.depth; });
        world3DTriangles.reserve(world3DTriangles.size() + modelDepthWorldTris.size());
        for (const DepthWorldTri& tri : modelDepthWorldTris) {
            world3DTriangles.push_back(tri.tri);
        }
    }
    if (!modelDepthTris.empty()) {
        std::sort(
            modelDepthTris.begin(),
            modelDepthTris.end(),
            [](const DepthTri& lhs, const DepthTri& rhs) { return lhs.depth > rhs.depth; });
        worldTriangles.reserve(worldTriangles.size() + modelDepthTris.size());
        for (const DepthTri& tri : modelDepthTris) {
            worldTriangles.push_back(tri.tri);
        }
    }
}

void appendBoardAndBench(const shared_board_grid::Config& cfg,
                         std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
                         std::vector<IRenderBackend::WorldTriangle>& world3DTriangles,
                         std::vector<IRenderBackend::DebugQuad>& worldBackgroundQuads,
                         std::vector<IRenderBackend::DebugLine>& lines,
                         shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug) {
    if (cfg.supportsWorldTriangles3D) {
        board_bench_geometry_cache::appendCachedBoardAndBench3D(
            cfg,
            worldTriangles,
            world3DTriangles,
            worldBackgroundQuads,
            lines);
        return;
    }

    shared_board_grid::appendBoardAndBench(
        cfg,
        worldTriangles,
        world3DTriangles,
        worldBackgroundQuads,
        lines,
        [&](const glm::vec3& a,
            const glm::vec3& b,
            const glm::vec3& c,
            const glm::vec3& d,
            float r,
            float g,
            float bl,
            float alpha) { projectedDebug.appendWorldQuad(a, b, c, d, r, g, bl, alpha); },
        [&](const glm::vec3& a,
            const glm::vec3& b,
            const glm::vec3& c,
            const glm::vec3& d,
            float r,
            float g,
            float bl,
            float alpha) { projectedDebug.appendProjectedQuad(a, b, c, d, r, g, bl, alpha); },
        [&](const glm::vec3& a,
            const glm::vec3& b,
            float r,
            float g,
            float bl,
            float alpha,
            float thickness) { projectedDebug.appendProjectedLine(a, b, r, g, bl, alpha, thickness); });
}

const TailFireVFXConfig& getPrimaryTailFireConfig() {
    return game::runtime::shared_tail_fire_coordinator::resolvePrimaryPlaybackConfig();
}

const runtime::render_model::MeshData* resolveModelMesh(
    const PokemonInstance& unit,
    const ::GameDataDb& dataDb,
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded) {
    std::string modelPath = unit.backendModelPath;
    if (modelPath.empty()) {
        if (!unit.animIndexCacheSourceModelPath.empty()) {
            modelPath = unit.animIndexCacheSourceModelPath;
        } else if (!unit.backendAnimDurationsSourceModelPath.empty()) {
            modelPath = unit.backendAnimDurationsSourceModelPath;
        } else {
            const PokemonStats* stats = dataDb.pokemon.getStats(unit.name);
            if (!stats || stats->resolveModel(unit.modelVariant).empty()) return nullptr;
            modelPath = "assets/models/" + stats->resolveModel(unit.modelVariant);
        }
    }

    runtime::render_model::MeshData* mesh = ensureBackendMeshLoaded(modelPath);
    if (!mesh || mesh->indices.size() < 3u) {
        return nullptr;
    }
    return mesh;
}

} // namespace game::runtime::shared_projected_scene


