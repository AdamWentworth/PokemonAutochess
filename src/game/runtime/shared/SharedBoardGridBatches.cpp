#include "game/runtime/shared/SharedBoardGridBatches.h"

#include <algorithm>

namespace game::runtime::shared_board_grid {

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

    const float boardSurfaceY = 0.006f;
    for (int r = 0; r < cfg.rows; ++r) {
        for (int c = 0; c < cfg.cols; ++c) {
            const float x0 = cfg.boardMinX + static_cast<float>(c) * cfg.worldCellSize;
            const float z0 = cfg.boardMinZ + static_cast<float>(r) * cfg.worldCellSize;
            const float x1 = x0 + cfg.worldCellSize;
            const float z1 = z0 + cfg.worldCellSize;
            const bool darkCell = ((r + c) % 2) == 0;
            const float cr = darkCell ? 0.07f : 0.10f;
            const float cg = darkCell ? 0.08f : 0.11f;
            const float cb = darkCell ? 0.09f : 0.12f;
            const float ca = darkCell ? 0.32f : 0.26f;
            const glm::vec3 qa(x0, boardSurfaceY, z0);
            const glm::vec3 qb(x1, boardSurfaceY, z0);
            const glm::vec3 qc(x1, boardSurfaceY, z1);
            const glm::vec3 qd(x0, boardSurfaceY, z1);
            if (cfg.supportsWorldTriangles3D) {
                appendWorldQuad(qa, qb, qc, qd, cr, cg, cb, ca);
            } else {
                appendProjectedQuad(qa, qb, qc, qd, cr, cg, cb, ca);
            }
        }
    }

    const float gridY = 0.0090f;
    const float gridHalfWidthWorld = std::max(0.0035f, cfg.worldCellSize * 0.0180f);
    for (int c = 0; c <= cfg.cols; ++c) {
        const float x = cfg.boardMinX + static_cast<float>(c) * cfg.worldCellSize;
        if (cfg.supportsWorldTriangles3D) {
            appendWorldQuad(
                glm::vec3(x - gridHalfWidthWorld, gridY, cfg.boardMinZ),
                glm::vec3(x + gridHalfWidthWorld, gridY, cfg.boardMinZ),
                glm::vec3(x + gridHalfWidthWorld, gridY, cfg.boardMaxZ),
                glm::vec3(x - gridHalfWidthWorld, gridY, cfg.boardMaxZ),
                0.82f,
                0.83f,
                0.85f,
                0.94f);
        } else {
            appendProjectedLine(
                glm::vec3(x, 0.01f, cfg.boardMinZ),
                glm::vec3(x, 0.01f, cfg.boardMaxZ),
                0.82f,
                0.83f,
                0.85f,
                0.94f,
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
                0.82f,
                0.83f,
                0.85f,
                0.94f);
        } else {
            appendProjectedLine(
                glm::vec3(cfg.boardMinX, 0.01f, z),
                glm::vec3(cfg.boardMaxX, 0.01f, z),
                0.82f,
                0.83f,
                0.85f,
                0.94f,
                cfg.line);
        }
    }

    {
        const int benchSlots = std::max(1, cfg.benchSlots);
        const float benchGapWorld = std::max(0.5f, cfg.worldCellSize * 0.5f);
        const float benchMinX = -0.5f * static_cast<float>(benchSlots) * cfg.worldCellSize;
        const float benchMaxX = benchMinX + static_cast<float>(benchSlots) * cfg.worldCellSize;
        const float benchMinZ = cfg.boardMaxZ + benchGapWorld;
        const float benchMaxZ = benchMinZ + cfg.worldCellSize;
        const float benchSurfaceY = boardSurfaceY;

        for (int slot = 0; slot < benchSlots; ++slot) {
            const float x0 = benchMinX + static_cast<float>(slot) * cfg.worldCellSize;
            const float x1 = x0 + cfg.worldCellSize;
            const bool darkCell = (slot % 2) == 0;
            const float cr = darkCell ? 0.075f : 0.105f;
            const float cg = darkCell ? 0.085f : 0.115f;
            const float cb = darkCell ? 0.095f : 0.125f;
            const float ca = darkCell ? 0.28f : 0.24f;
            const glm::vec3 qa(x0, benchSurfaceY, benchMinZ);
            const glm::vec3 qb(x1, benchSurfaceY, benchMinZ);
            const glm::vec3 qc(x1, benchSurfaceY, benchMaxZ);
            const glm::vec3 qd(x0, benchSurfaceY, benchMaxZ);
            if (cfg.supportsWorldTriangles3D) {
                appendWorldQuad(qa, qb, qc, qd, cr, cg, cb, ca);
            } else {
                appendProjectedQuad(qa, qb, qc, qd, cr, cg, cb, ca);
            }
        }

        for (int c = 0; c <= benchSlots; ++c) {
            const float x = benchMinX + static_cast<float>(c) * cfg.worldCellSize;
            if (cfg.supportsWorldTriangles3D) {
                appendWorldQuad(
                    glm::vec3(x - gridHalfWidthWorld, gridY, benchMinZ),
                    glm::vec3(x + gridHalfWidthWorld, gridY, benchMinZ),
                    glm::vec3(x + gridHalfWidthWorld, gridY, benchMaxZ),
                    glm::vec3(x - gridHalfWidthWorld, gridY, benchMaxZ),
                    0.82f,
                    0.83f,
                    0.85f,
                    0.94f);
            } else {
                appendProjectedLine(
                    glm::vec3(x, 0.01f, benchMinZ),
                    glm::vec3(x, 0.01f, benchMaxZ),
                    0.82f,
                    0.83f,
                    0.85f,
                    0.94f,
                    cfg.line);
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
                    0.82f,
                    0.83f,
                    0.85f,
                    0.94f);
            } else {
                appendProjectedLine(
                    glm::vec3(benchMinX, 0.01f, z),
                    glm::vec3(benchMaxX, 0.01f, z),
                    0.82f,
                    0.83f,
                    0.85f,
                    0.94f,
                    cfg.line);
            }
        }
    }

    if (worldTriangles.size() == boardTrianglesStart2D && world3DTriangles.size() == boardTrianglesStart3D) {
        IRenderBackend::DebugQuad boardFallback;
        boardFallback.x = cfg.boardX;
        boardFallback.y = cfg.boardY;
        boardFallback.w = cfg.boardW;
        boardFallback.h = cfg.boardH;
        boardFallback.r = 0.06f;
        boardFallback.g = 0.07f;
        boardFallback.b = 0.08f;
        boardFallback.a = 0.92f;
        worldBackgroundQuads.push_back(boardFallback);

        for (int r = 0; r < cfg.rows; ++r) {
            for (int c = 0; c < cfg.cols; ++c) {
                IRenderBackend::DebugQuad cell;
                cell.x = cfg.boardX + cfg.cellW * static_cast<float>(c);
                cell.y = cfg.boardY + cfg.cellH * static_cast<float>(r);
                cell.w = cfg.cellW;
                cell.h = cfg.cellH;
                const bool darkCell = ((r + c) % 2) == 0;
                cell.r = darkCell ? 0.09f : 0.14f;
                cell.g = darkCell ? 0.14f : 0.19f;
                cell.b = darkCell ? 0.19f : 0.25f;
                cell.a = darkCell ? 0.34f : 0.26f;
                worldBackgroundQuads.push_back(cell);
            }
        }

        for (int c = 0; c <= cfg.cols; ++c) {
            IRenderBackend::DebugLine vLine;
            vLine.x1 = cfg.boardX + cfg.cellW * static_cast<float>(c);
            vLine.y1 = cfg.boardY;
            vLine.x2 = vLine.x1;
            vLine.y2 = cfg.boardY + cfg.boardH;
            vLine.thickness = cfg.line;
            vLine.r = 0.26f;
            vLine.g = 0.38f;
            vLine.b = 0.47f;
            vLine.a = 0.96f;
            lines.push_back(vLine);
        }
        for (int r = 0; r <= cfg.rows; ++r) {
            IRenderBackend::DebugLine hLine;
            hLine.x1 = cfg.boardX;
            hLine.y1 = cfg.boardY + cfg.cellH * static_cast<float>(r);
            hLine.x2 = cfg.boardX + cfg.boardW;
            hLine.y2 = hLine.y1;
            hLine.thickness = cfg.line;
            hLine.r = 0.26f;
            hLine.g = 0.38f;
            hLine.b = 0.47f;
            hLine.a = 0.96f;
            lines.push_back(hLine);
        }
    }
}

} // namespace game::runtime::shared_board_grid

