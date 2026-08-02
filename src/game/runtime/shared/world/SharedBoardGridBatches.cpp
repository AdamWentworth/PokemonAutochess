#include "game/runtime/shared/world/SharedBoardGridBatches.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace game::runtime::shared_board_grid {

namespace {

constexpr float kGridFillAlphaEpsilon = 0.001f;

bool shouldEmitFillQuad(const std::array<float, 4>& color) {
    return color[3] > kGridFillAlphaEpsilon;
}

bool shouldEmitLine(const std::array<float, 4>& color) {
    return color[3] > kGridFillAlphaEpsilon;
}

} // namespace

const VisualTheme& defaultVisualTheme() {
    static const VisualTheme theme{};
    return theme;
}

void appendBoardAndBench(
    const Config& cfg,
    std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
    std::vector<IRenderBackend::WorldTriangle>& world3DTriangles,
    std::vector<IRenderBackend::DebugQuad>& worldBackgroundQuads,
    std::vector<IRenderBackend::DebugLine>& lines,
    const AppendWorldQuadFn& appendWorldQuad,
    const AppendProjectedQuadFn& appendProjectedQuad,
    const AppendProjectedLineFn& appendProjectedLine) {
    const std::size_t boardTrianglesStart2D = worldTriangles.size();
    const std::size_t boardTrianglesStart3D = world3DTriangles.size();
    const VisualTheme& theme = cfg.visualTheme ? *cfg.visualTheme : defaultVisualTheme();

    const float boardSurfaceY = theme.boardSurfaceY;
    for (int r = 0; r < cfg.rows; ++r) {
        for (int c = 0; c < cfg.cols; ++c) {
            const float x0 = cfg.boardMinX + static_cast<float>(c) * cfg.worldCellSize;
            const float z0 = cfg.boardMinZ + static_cast<float>(r) * cfg.worldCellSize;
            const float x1 = x0 + cfg.worldCellSize;
            const float z1 = z0 + cfg.worldCellSize;
            const bool darkCell = ((r + c) % 2) == 0;
            const auto& color = darkCell ? theme.boardCellDark : theme.boardCellLight;
            const glm::vec3 qa(x0, boardSurfaceY, z0);
            const glm::vec3 qb(x1, boardSurfaceY, z0);
            const glm::vec3 qc(x1, boardSurfaceY, z1);
            const glm::vec3 qd(x0, boardSurfaceY, z1);
            if (shouldEmitFillQuad(color)) {
                if (cfg.supportsWorldTriangles3D) {
                    appendWorldQuad(qa, qb, qc, qd, color[0], color[1], color[2], color[3]);
                } else {
                    appendProjectedQuad(qa, qb, qc, qd, color[0], color[1], color[2], color[3]);
                }
            }
        }
    }

    const float gridY = theme.gridY;
    const float gridHalfWidthWorld =
        std::max(theme.gridHalfWidthMin, cfg.worldCellSize * theme.gridHalfWidthScale);
    if (cfg.emitFlatGrid && shouldEmitLine(theme.gridLine)) {
    for (int c = 0; c <= cfg.cols; ++c) {
        const float x = cfg.boardMinX + static_cast<float>(c) * cfg.worldCellSize;
        if (cfg.supportsWorldTriangles3D) {
            appendWorldQuad(
                glm::vec3(x - gridHalfWidthWorld, gridY, cfg.boardMinZ),
                glm::vec3(x + gridHalfWidthWorld, gridY, cfg.boardMinZ),
                glm::vec3(x + gridHalfWidthWorld, gridY, cfg.boardMaxZ),
                glm::vec3(x - gridHalfWidthWorld, gridY, cfg.boardMaxZ),
                theme.gridLine[0],
                theme.gridLine[1],
                theme.gridLine[2],
                theme.gridLine[3]);
        } else {
            appendProjectedLine(
                glm::vec3(x, 0.01f, cfg.boardMinZ),
                glm::vec3(x, 0.01f, cfg.boardMaxZ),
                theme.gridLine[0],
                theme.gridLine[1],
                theme.gridLine[2],
                theme.gridLine[3],
                cfg.line);
        }
    }
    for (int r = 0; r <= cfg.rows; ++r) {
        const float z = cfg.boardMinZ + static_cast<float>(r) * cfg.worldCellSize;
        if (cfg.supportsWorldTriangles3D) {
            appendWorldQuad(
                glm::vec3(cfg.boardMinX, gridY, z - gridHalfWidthWorld),
                glm::vec3(cfg.boardMaxX, gridY, z - gridHalfWidthWorld),
                glm::vec3(cfg.boardMaxX, gridY, z + gridHalfWidthWorld),
                glm::vec3(cfg.boardMinX, gridY, z + gridHalfWidthWorld),
                theme.gridLine[0],
                theme.gridLine[1],
                theme.gridLine[2],
                theme.gridLine[3]);
        } else {
            appendProjectedLine(
                glm::vec3(cfg.boardMinX, 0.01f, z),
                glm::vec3(cfg.boardMaxX, 0.01f, z),
                theme.gridLine[0],
                theme.gridLine[1],
                theme.gridLine[2],
                theme.gridLine[3],
                cfg.line);
        }
    }
    }

    {
        const int benchSlots = std::max(1, cfg.benchSlots);
        const float benchGapWorld = std::max(
            theme.benchGapMin,
            cfg.worldCellSize *
                static_cast<float>(std::max(0, cfg.benchGapCells)) *
                theme.benchGapScale);
        const float boardCenterX =
            (cfg.boardMinX + cfg.boardMaxX) * 0.5f;
        const float benchMinX = boardCenterX -
            0.5f * static_cast<float>(benchSlots) * cfg.worldCellSize;
        const float benchMaxX = benchMinX + static_cast<float>(benchSlots) * cfg.worldCellSize;
        const float benchSurfaceY = boardSurfaceY;
        const auto appendBench = [&](float benchMinZ) {
            const float benchMaxZ = benchMinZ + cfg.worldCellSize;
            for (int slot = 0; slot < benchSlots; ++slot) {
                const float x0 = benchMinX + static_cast<float>(slot) * cfg.worldCellSize;
                const float x1 = x0 + cfg.worldCellSize;
                const bool darkCell = (slot % 2) == 0;
                const auto& color = darkCell ? theme.benchCellDark : theme.benchCellLight;
                const glm::vec3 qa(x0, benchSurfaceY, benchMinZ);
                const glm::vec3 qb(x1, benchSurfaceY, benchMinZ);
                const glm::vec3 qc(x1, benchSurfaceY, benchMaxZ);
                const glm::vec3 qd(x0, benchSurfaceY, benchMaxZ);
                if (shouldEmitFillQuad(color)) {
                    if (cfg.supportsWorldTriangles3D) {
                        appendWorldQuad(qa, qb, qc, qd, color[0], color[1], color[2], color[3]);
                    } else {
                        appendProjectedQuad(qa, qb, qc, qd, color[0], color[1], color[2], color[3]);
                    }
                }
            }
            if (cfg.emitFlatGrid && shouldEmitLine(theme.gridLine)) {
            for (int c = 0; c <= benchSlots; ++c) {
                const float x = benchMinX + static_cast<float>(c) * cfg.worldCellSize;
                if (cfg.supportsWorldTriangles3D) {
                    appendWorldQuad(
                        glm::vec3(x - gridHalfWidthWorld, gridY, benchMinZ),
                        glm::vec3(x + gridHalfWidthWorld, gridY, benchMinZ),
                        glm::vec3(x + gridHalfWidthWorld, gridY, benchMaxZ),
                        glm::vec3(x - gridHalfWidthWorld, gridY, benchMaxZ),
                        theme.gridLine[0], theme.gridLine[1], theme.gridLine[2], theme.gridLine[3]);
                } else {
                    appendProjectedLine(
                        glm::vec3(x, 0.01f, benchMinZ),
                        glm::vec3(x, 0.01f, benchMaxZ),
                        theme.gridLine[0], theme.gridLine[1], theme.gridLine[2], theme.gridLine[3], cfg.line);
                }
            }
            for (int r = 0; r <= 1; ++r) {
                const float z = benchMinZ + static_cast<float>(r) * cfg.worldCellSize;
                if (cfg.supportsWorldTriangles3D) {
                    appendWorldQuad(
                        glm::vec3(benchMinX, gridY, z - gridHalfWidthWorld),
                        glm::vec3(benchMaxX, gridY, z - gridHalfWidthWorld),
                        glm::vec3(benchMaxX, gridY, z + gridHalfWidthWorld),
                        glm::vec3(benchMinX, gridY, z + gridHalfWidthWorld),
                        theme.gridLine[0], theme.gridLine[1], theme.gridLine[2], theme.gridLine[3]);
                } else {
                    appendProjectedLine(
                        glm::vec3(benchMinX, 0.01f, z),
                        glm::vec3(benchMaxX, 0.01f, z),
                        theme.gridLine[0], theme.gridLine[1], theme.gridLine[2], theme.gridLine[3], cfg.line);
                }
            }
            }
        };
        appendBench(cfg.boardMaxZ + benchGapWorld);
        appendBench(cfg.boardMinZ - benchGapWorld - cfg.worldCellSize);
    }

    if (worldTriangles.size() == boardTrianglesStart2D && world3DTriangles.size() == boardTrianglesStart3D) {
        if (shouldEmitFillQuad(theme.fallbackBoardBackground)) {
            IRenderBackend::DebugQuad boardFallback;
            boardFallback.x = cfg.boardX;
            boardFallback.y = cfg.boardY;
            boardFallback.w = cfg.boardW;
            boardFallback.h = cfg.boardH;
            boardFallback.r = theme.fallbackBoardBackground[0];
            boardFallback.g = theme.fallbackBoardBackground[1];
            boardFallback.b = theme.fallbackBoardBackground[2];
            boardFallback.a = theme.fallbackBoardBackground[3];
            worldBackgroundQuads.push_back(boardFallback);
        }

        for (int r = 0; r < cfg.rows; ++r) {
            for (int c = 0; c < cfg.cols; ++c) {
                const bool darkCell = ((r + c) % 2) == 0;
                const auto& color = darkCell ? theme.fallbackBoardCellDark : theme.fallbackBoardCellLight;
                if (!shouldEmitFillQuad(color)) {
                    continue;
                }
                IRenderBackend::DebugQuad cell;
                cell.x = cfg.boardX + cfg.cellW * static_cast<float>(c);
                cell.y = cfg.boardY + cfg.cellH * static_cast<float>(r);
                cell.w = cfg.cellW;
                cell.h = cfg.cellH;
                cell.r = color[0];
                cell.g = color[1];
                cell.b = color[2];
                cell.a = color[3];
                worldBackgroundQuads.push_back(cell);
            }
        }

        if (cfg.emitFlatGrid && shouldEmitLine(theme.fallbackGridLine)) {
        for (int c = 0; c <= cfg.cols; ++c) {
            IRenderBackend::DebugLine vLine;
            vLine.x1 = cfg.boardX + cfg.cellW * static_cast<float>(c);
            vLine.y1 = cfg.boardY;
            vLine.x2 = vLine.x1;
            vLine.y2 = cfg.boardY + cfg.boardH;
            vLine.thickness = cfg.line;
            vLine.r = theme.fallbackGridLine[0];
            vLine.g = theme.fallbackGridLine[1];
            vLine.b = theme.fallbackGridLine[2];
            vLine.a = theme.fallbackGridLine[3];
            lines.push_back(vLine);
        }
        for (int r = 0; r <= cfg.rows; ++r) {
            IRenderBackend::DebugLine hLine;
            hLine.x1 = cfg.boardX;
            hLine.y1 = cfg.boardY + cfg.cellH * static_cast<float>(r);
            hLine.x2 = cfg.boardX + cfg.boardW;
            hLine.y2 = hLine.y1;
            hLine.thickness = cfg.line;
            hLine.r = theme.fallbackGridLine[0];
            hLine.g = theme.fallbackGridLine[1];
            hLine.b = theme.fallbackGridLine[2];
            hLine.a = theme.fallbackGridLine[3];
            lines.push_back(hLine);
        }
        }
    }
}

void appendTerrainConformingBoardAndBench(
    const Config& cfg,
    std::vector<shared_world_batches::WorldIndexedBatch>& out) {
    if (!cfg.supportsWorldTriangles3D || !cfg.sampleSurfaceHeight ||
        cfg.rows <= 0 || cfg.cols <= 0 || cfg.worldCellSize <= 0.0f) {
        return;
    }

    const VisualTheme& theme =
        cfg.visualTheme ? *cfg.visualTheme : defaultVisualTheme();
    shared_world_batches::WorldIndexedBatch batch{};
    batch.alphaMode = 2u;
    batch.blendMode = 0u;
    batch.depthTestEnabled = 1u;
    batch.materialMode = 0u;
    batch.metallicFactor = 0.0f;
    batch.roughnessFactor = 1.0f;
    batch.preserveSubmissionOrder = true;

    const int segmentsPerCell =
        std::max(1, theme.terrainSegmentsPerCell);
    const float surfaceOffset =
        std::max(0.002f, theme.terrainSurfaceOffset);
    const float internalHalfWidth = std::max(
        0.0035f,
        cfg.worldCellSize * theme.terrainGridHalfWidthScale);
    const float boundaryHalfWidth = std::max(
        internalHalfWidth,
        cfg.worldCellSize * theme.terrainBoundaryHalfWidthScale);
    const float maximumSegmentRise =
        std::max(0.18f, cfg.worldCellSize * 0.38f);

    const auto appendVertex =
        [&](const glm::vec3& p, const std::array<float, 4>& color) {
            batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                .x = p.x,
                .y = p.y,
                .z = p.z,
                .u = 0.0f,
                .v = 0.0f,
                .r = color[0],
                .g = color[1],
                .b = color[2],
                .a = color[3],
                .nx = 0.0f,
                .ny = 1.0f,
                .nz = 0.0f,
                .tx = 1.0f,
                .ty = 0.0f,
                .tz = 0.0f,
                .tw = 1.0f,
            });
        };
    const auto appendRibbon =
        [&](const glm::vec2& start,
            const glm::vec2& end,
            float halfWidth,
            const std::array<float, 4>& color) {
            if (!shouldEmitLine(color)) {
                return;
            }
            const glm::vec2 delta = end - start;
            const float length = glm::length(delta);
            if (length <= 0.0001f) {
                return;
            }
            const glm::vec2 direction = delta / length;
            const glm::vec2 perpendicular(
                -direction.y * halfWidth,
                direction.x * halfWidth);
            const int segmentCount = std::max(
                1,
                static_cast<int>(std::ceil(
                    length / cfg.worldCellSize *
                    static_cast<float>(segmentsPerCell))));
            for (int segment = 0; segment < segmentCount; ++segment) {
                const float t0 = static_cast<float>(segment) /
                    static_cast<float>(segmentCount);
                const float t1 = static_cast<float>(segment + 1) /
                    static_cast<float>(segmentCount);
                const glm::vec2 p0 = start + delta * t0;
                const glm::vec2 p1 = start + delta * t1;
                float y0 = 0.0f;
                float y1 = 0.0f;
                if (!cfg.sampleSurfaceHeight(p0.x, p0.y, y0) ||
                    !cfg.sampleSurfaceHeight(p1.x, p1.y, y1) ||
                    !std::isfinite(y0) || !std::isfinite(y1) ||
                    std::abs(y1 - y0) > maximumSegmentRise) {
                    continue;
                }
                y0 += surfaceOffset;
                y1 += surfaceOffset;
                const std::uint32_t base =
                    static_cast<std::uint32_t>(batch.vertices.size());
                appendVertex(
                    {p0.x - perpendicular.x,
                     y0,
                     p0.y - perpendicular.y},
                    color);
                appendVertex(
                    {p0.x + perpendicular.x,
                     y0,
                     p0.y + perpendicular.y},
                    color);
                appendVertex(
                    {p1.x + perpendicular.x,
                     y1,
                     p1.y + perpendicular.y},
                    color);
                appendVertex(
                    {p1.x - perpendicular.x,
                     y1,
                     p1.y - perpendicular.y},
                    color);
                batch.indices.insert(
                    batch.indices.end(),
                    {base + 0u, base + 1u, base + 2u,
                     base + 0u, base + 2u, base + 3u});
            }
        };

    for (int c = 0; c <= cfg.cols; ++c) {
        const bool boundary = c == 0 || c == cfg.cols;
        const float x = cfg.boardMinX +
            static_cast<float>(c) * cfg.worldCellSize;
        appendRibbon(
            {x, cfg.boardMinZ},
            {x, cfg.boardMaxZ},
            boundary ? boundaryHalfWidth : internalHalfWidth,
            boundary ? theme.boardBoundaryLine : theme.gridLine);
    }
    for (int r = 0; r <= cfg.rows; ++r) {
        const bool boundary = r == 0 || r == cfg.rows;
        const float z = cfg.boardMinZ +
            static_cast<float>(r) * cfg.worldCellSize;
        appendRibbon(
            {cfg.boardMinX, z},
            {cfg.boardMaxX, z},
            boundary ? boundaryHalfWidth : internalHalfWidth,
            boundary ? theme.boardBoundaryLine : theme.gridLine);
    }

    const int benchSlots = std::max(1, cfg.benchSlots);
    const float benchGapWorld = std::max(
        theme.benchGapMin,
        cfg.worldCellSize *
            static_cast<float>(std::max(0, cfg.benchGapCells)) *
            theme.benchGapScale);
    const float boardCenterX =
        (cfg.boardMinX + cfg.boardMaxX) * 0.5f;
    const float benchMinX = boardCenterX -
        0.5f * static_cast<float>(benchSlots) * cfg.worldCellSize;
    const float benchMaxX = benchMinX +
        static_cast<float>(benchSlots) * cfg.worldCellSize;
    const auto appendBench =
        [&](float benchMinZ, bool northBench) {
            const float benchMaxZ = benchMinZ + cfg.worldCellSize;
            for (int c = 0; c <= benchSlots; ++c) {
                const bool boundary = c == 0 || c == benchSlots;
                const float x = benchMinX +
                    static_cast<float>(c) * cfg.worldCellSize;
                appendRibbon(
                    {x, benchMinZ},
                    {x, benchMaxZ},
                    boundary ? boundaryHalfWidth : internalHalfWidth,
                    boundary
                        ? theme.benchBoundaryLine
                        : theme.benchGridLine);
            }
            const bool sharesBoardEdge = benchGapWorld <= 0.0001f;
            if (!(sharesBoardEdge && northBench)) {
                appendRibbon(
                    {benchMinX, benchMinZ},
                    {benchMaxX, benchMinZ},
                    boundaryHalfWidth,
                    theme.benchBoundaryLine);
            }
            if (!(sharesBoardEdge && !northBench)) {
                appendRibbon(
                    {benchMinX, benchMaxZ},
                    {benchMaxX, benchMaxZ},
                    boundaryHalfWidth,
                    theme.benchBoundaryLine);
            }
        };
    appendBench(cfg.boardMaxZ + benchGapWorld, true);
    appendBench(
        cfg.boardMinZ - benchGapWorld - cfg.worldCellSize,
        false);

    if (!batch.vertices.empty() && batch.indices.size() >= 3u) {
        out.push_back(std::move(batch));
    }
}

} // namespace game::runtime::shared_board_grid
