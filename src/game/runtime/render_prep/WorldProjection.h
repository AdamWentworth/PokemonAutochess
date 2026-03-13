#pragma once

#include <algorithm>
#include <cmath>
#include <utility>

namespace game::runtime::render_prep_projection {

struct BoardBounds {
    float minX = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxZ = 0.0f;
};

inline float safeCellSize(float cellSize) {
    return std::max(0.05f, cellSize);
}

inline float boardOriginX(int cols, float cellSize) {
    const int safeCols = std::max(1, cols);
    const float cell = safeCellSize(cellSize);
    return -((static_cast<float>(safeCols) * cell) * 0.5f) + cell * 0.5f;
}

inline float boardOriginZ(int rows, float cellSize) {
    const int safeRows = std::max(1, rows);
    const float cell = safeCellSize(cellSize);
    return -((static_cast<float>(safeRows) * cell) * 0.5f) + cell * 0.5f;
}

inline BoardBounds computeBoardBounds(int cols, int rows, float cellSize) {
    const int safeCols = std::max(1, cols);
    const int safeRows = std::max(1, rows);
    const float cell = safeCellSize(cellSize);
    const float originX = boardOriginX(safeCols, cell);
    const float originZ = boardOriginZ(safeRows, cell);

    BoardBounds out;
    out.minX = originX - cell * 0.5f;
    out.minZ = originZ - cell * 0.5f;
    out.maxX = originX + (static_cast<float>(safeCols) - 0.5f) * cell;
    out.maxZ = originZ + (static_cast<float>(safeRows) - 0.5f) * cell;
    return out;
}

inline std::pair<float, float> worldToBoardUv(float worldX,
                                              float worldZ,
                                              int cols,
                                              int rows,
                                              float cellSize) {
    const int safeCols = std::max(1, cols);
    const int safeRows = std::max(1, rows);
    const float cell = safeCellSize(cellSize);
    const float originX = boardOriginX(safeCols, cell);
    const float originZ = boardOriginZ(safeRows, cell);
    const float col = (worldX - originX) / cell;
    const float row = (worldZ - originZ) / cell;
    return {
        (col + 0.5f) / static_cast<float>(safeCols),
        (row + 0.5f) / static_cast<float>(safeRows),
    };
}

inline int worldToBenchSlot(float worldX, int benchSlots, float cellSize) {
    const int safeSlots = std::max(1, benchSlots);
    const float cell = safeCellSize(cellSize);
    const float totalWidth = static_cast<float>(safeSlots) * cell;
    const float startX = -totalWidth * 0.5f;
    int slot = static_cast<int>(std::round((worldX - (startX + cell * 0.5f)) / cell));
    slot = std::clamp(slot, 0, safeSlots - 1);
    return slot;
}

} // namespace game::runtime::render_prep_projection

