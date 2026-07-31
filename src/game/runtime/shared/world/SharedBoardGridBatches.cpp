#include "game/runtime/shared/world/SharedBoardGridBatches.h"

#include <algorithm>

namespace game::runtime::shared_board_grid {

namespace {

constexpr float kGridFillAlphaEpsilon = 0.001f;

bool shouldEmitFillQuad(const std::array<float, 4>& color) {
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

    {
        const int benchSlots = std::max(1, cfg.benchSlots);
        const float benchGapWorld = std::max(theme.benchGapMin, cfg.worldCellSize * theme.benchGapScale);
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

} // namespace game::runtime::shared_board_grid
