#pragma once

#include "engine/render/Camera3D.h"
#include "game/runtime/render_prep/WorldProjection.h"

#include <algorithm>

namespace game::camera_pan_clamp {

struct Bounds {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
};

inline Bounds buildBoardSafeBounds(
    const runtime::render_prep_projection::BoardBounds& boardBounds,
    float cellSize) {
    const float safeCell =
        runtime::render_prep_projection::safeCellSize(cellSize);
    const float width = std::max(0.001f, boardBounds.maxX - boardBounds.minX);
    const float depth = std::max(0.001f, boardBounds.maxZ - boardBounds.minZ);
    const float centerX = (boardBounds.minX + boardBounds.maxX) * 0.5f;
    const float centerZ = (boardBounds.minZ + boardBounds.maxZ) * 0.5f;

    // Keep the target near the board center so panning can reframe slightly
    // without ever letting the board drift out of view.
    const float sideExtent =
        std::max(safeCell * 0.75f, std::min(width * 0.22f, safeCell * 2.0f));
    const float backExtent =
        std::max(safeCell * 0.60f, std::min(depth * 0.18f, safeCell * 1.5f));
    const float frontExtent =
        std::max(safeCell * 0.75f, std::min(depth * 0.24f, safeCell * 2.0f));

    Bounds out;
    out.minX = centerX - sideExtent;
    out.maxX = centerX + sideExtent;
    out.minZ = centerZ - backExtent;
    out.maxZ = centerZ + frontExtent;
    return out;
}

inline Bounds buildBoardSafeBounds(int cols, int rows, float cellSize) {
    return buildBoardSafeBounds(
        runtime::render_prep_projection::computeBoardBounds(cols, rows, cellSize),
        cellSize);
}

inline void clampCameraPan(Camera3D& camera, const Bounds& bounds) {
    const glm::vec3 oldTarget = camera.getTarget();
    const glm::vec3 offset = camera.getPosition() - oldTarget;

    glm::vec3 clampedTarget = oldTarget;
    clampedTarget.x = std::clamp(clampedTarget.x, bounds.minX, bounds.maxX);
    clampedTarget.z = std::clamp(clampedTarget.z, bounds.minZ, bounds.maxZ);

    camera.lookAt(clampedTarget);
    camera.setPosition(clampedTarget + offset);
}

} // namespace game::camera_pan_clamp
