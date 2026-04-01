#include "game/runtime/shared/projected/world_scene/SharedProjectedBoardBenchGeometryCache.h"

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_scene::board_bench_geometry_cache {
namespace {

struct BoardBenchGeometryCacheKey {
    bool supportsWorldTriangles3D = false;
    int rows = 0;
    int cols = 0;
    int benchSlots = 0;
    float worldCellSize = 0.0f;
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
    float line = 0.0f;
    const shared_board_grid::VisualTheme* visualTheme = nullptr;

    bool operator==(const BoardBenchGeometryCacheKey& other) const {
        return supportsWorldTriangles3D == other.supportsWorldTriangles3D &&
               rows == other.rows &&
               cols == other.cols &&
               benchSlots == other.benchSlots &&
               worldCellSize == other.worldCellSize &&
               boardMinX == other.boardMinX &&
               boardMinZ == other.boardMinZ &&
               boardMaxX == other.boardMaxX &&
               boardMaxZ == other.boardMaxZ &&
               boardX == other.boardX &&
               boardY == other.boardY &&
               boardW == other.boardW &&
               boardH == other.boardH &&
               cellW == other.cellW &&
               cellH == other.cellH &&
               line == other.line &&
               visualTheme == other.visualTheme;
    }
};

struct BoardBenchGeometryCache {
    bool valid = false;
    BoardBenchGeometryCacheKey key{};
    std::vector<IRenderBackend::DebugTriangle> worldTriangles;
    std::vector<IRenderBackend::WorldTriangle> world3DTriangles;
    std::vector<IRenderBackend::DebugQuad> worldBackgroundQuads;
    std::vector<IRenderBackend::DebugLine> lines;
};

BoardBenchGeometryCacheKey makeBoardBenchGeometryCacheKey(const shared_board_grid::Config& cfg) {
    BoardBenchGeometryCacheKey key{};
    key.supportsWorldTriangles3D = cfg.supportsWorldTriangles3D;
    key.rows = cfg.rows;
    key.cols = cfg.cols;
    key.benchSlots = cfg.benchSlots;
    key.worldCellSize = cfg.worldCellSize;
    key.boardMinX = cfg.boardMinX;
    key.boardMinZ = cfg.boardMinZ;
    key.boardMaxX = cfg.boardMaxX;
    key.boardMaxZ = cfg.boardMaxZ;
    key.boardX = cfg.boardX;
    key.boardY = cfg.boardY;
    key.boardW = cfg.boardW;
    key.boardH = cfg.boardH;
    key.cellW = cfg.cellW;
    key.cellH = cfg.cellH;
    key.line = cfg.line;
    key.visualTheme = cfg.visualTheme ? cfg.visualTheme : &shared_board_grid::defaultVisualTheme();
    return key;
}

void appendWorldQuadToTriangles(std::vector<IRenderBackend::WorldTriangle>& out,
                                const glm::vec3& a,
                                const glm::vec3& b,
                                const glm::vec3& c,
                                const glm::vec3& d,
                                float r,
                                float g,
                                float bl,
                                float alpha) {
    IRenderBackend::WorldTriangle t0{};
    t0.x1 = a.x; t0.y1 = a.y; t0.z1 = a.z;
    t0.x2 = b.x; t0.y2 = b.y; t0.z2 = b.z;
    t0.x3 = c.x; t0.y3 = c.y; t0.z3 = c.z;
    t0.r = r; t0.g = g; t0.b = bl; t0.a = alpha;
    out.push_back(t0);

    IRenderBackend::WorldTriangle t1{};
    t1.x1 = a.x; t1.y1 = a.y; t1.z1 = a.z;
    t1.x2 = c.x; t1.y2 = c.y; t1.z2 = c.z;
    t1.x3 = d.x; t1.y3 = d.y; t1.z3 = d.z;
    t1.r = r; t1.g = g; t1.b = bl; t1.a = alpha;
    out.push_back(t1);
}

} // namespace

void appendCachedBoardAndBench3D(const shared_board_grid::Config& cfg,
                                 std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
                                 std::vector<IRenderBackend::WorldTriangle>& world3DTriangles,
                                 std::vector<IRenderBackend::DebugQuad>& worldBackgroundQuads,
                                 std::vector<IRenderBackend::DebugLine>& lines) {
    static thread_local BoardBenchGeometryCache cache;
    const BoardBenchGeometryCacheKey key = makeBoardBenchGeometryCacheKey(cfg);
    if (!cache.valid || !(cache.key == key)) {
        cache.valid = true;
        cache.key = key;
        cache.worldTriangles.clear();
        cache.world3DTriangles.clear();
        cache.worldBackgroundQuads.clear();
        cache.lines.clear();

        shared_board_grid::appendBoardAndBench(
            cfg,
            cache.worldTriangles,
            cache.world3DTriangles,
            cache.worldBackgroundQuads,
            cache.lines,
            [&](const glm::vec3& a,
                const glm::vec3& b,
                const glm::vec3& c,
                const glm::vec3& d,
                float r,
                float g,
                float bl,
                float alpha) {
                appendWorldQuadToTriangles(cache.world3DTriangles, a, b, c, d, r, g, bl, alpha);
            },
            [&](const glm::vec3&,
                const glm::vec3&,
                const glm::vec3&,
                const glm::vec3&,
                float,
                float,
                float,
                float) {},
            [&](const glm::vec3&,
                const glm::vec3&,
                float,
                float,
                float,
                float,
                float) {});
    }

    if (!cache.worldTriangles.empty()) {
        worldTriangles.reserve(worldTriangles.size() + cache.worldTriangles.size());
        worldTriangles.insert(
            worldTriangles.end(),
            cache.worldTriangles.begin(),
            cache.worldTriangles.end());
    }
    if (!cache.world3DTriangles.empty()) {
        world3DTriangles.reserve(world3DTriangles.size() + cache.world3DTriangles.size());
        world3DTriangles.insert(
            world3DTriangles.end(),
            cache.world3DTriangles.begin(),
            cache.world3DTriangles.end());
    }
    if (!cache.worldBackgroundQuads.empty()) {
        worldBackgroundQuads.reserve(worldBackgroundQuads.size() + cache.worldBackgroundQuads.size());
        worldBackgroundQuads.insert(
            worldBackgroundQuads.end(),
            cache.worldBackgroundQuads.begin(),
            cache.worldBackgroundQuads.end());
    }
    if (!cache.lines.empty()) {
        lines.reserve(lines.size() + cache.lines.size());
        lines.insert(lines.end(), cache.lines.begin(), cache.lines.end());
    }
}

} // namespace game::runtime::shared_projected_scene::board_bench_geometry_cache

