#include "game/runtime/render_prep/BackendWorldProjection.h"

#include <cmath>
#include <string>

namespace {

bool approx(float a, float b, float eps = 0.0005f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool test_backend_world_projection_contract(std::string& outFail) {
    using game::runtime::backendview::computeBoardBounds;
    using game::runtime::backendview::worldToBenchSlot;
    using game::runtime::backendview::worldToBoardUv;

    const int cols = 8;
    const int rows = 4;
    const float cell = 1.0f;

    {
        const auto uv = worldToBoardUv(-3.5f, -1.5f, cols, rows, cell);
        if (!approx(uv.first, 0.0625f) || !approx(uv.second, 0.125f)) {
            outFail = "worldToBoardUv origin-cell center mapping mismatch";
            return false;
        }
    }

    {
        const auto uv = worldToBoardUv(-0.5f, 0.5f, cols, rows, cell);
        if (!approx(uv.first, 0.4375f) || !approx(uv.second, 0.625f)) {
            outFail = "worldToBoardUv mid-board mapping mismatch";
            return false;
        }
    }

    {
        const auto uv = worldToBoardUv(0.0f, 0.0f, cols, rows, 0.0f);
        if (!(uv.first > 0.0f && uv.first < 1.0f && uv.second > 0.0f && uv.second < 1.0f)) {
            outFail = "worldToBoardUv should clamp invalid cell size to a safe positive value";
            return false;
        }
    }

    if (worldToBenchSlot(-2.5f, 6, cell) != 0) {
        outFail = "worldToBenchSlot left edge mapping mismatch";
        return false;
    }
    if (worldToBenchSlot(2.5f, 6, cell) != 5) {
        outFail = "worldToBenchSlot right edge mapping mismatch";
        return false;
    }
    if (worldToBenchSlot(100.0f, 6, cell) != 5) {
        outFail = "worldToBenchSlot should clamp high out-of-range";
        return false;
    }
    if (worldToBenchSlot(-100.0f, 6, cell) != 0) {
        outFail = "worldToBenchSlot should clamp low out-of-range";
        return false;
    }

    {
        const auto bounds = computeBoardBounds(8, 4, 1.0f);
        if (!approx(bounds.minX, -4.0f) || !approx(bounds.maxX, 4.0f) ||
            !approx(bounds.minZ, -2.0f) || !approx(bounds.maxZ, 2.0f)) {
            outFail = "computeBoardBounds should map to expected world extents";
            return false;
        }
    }

    return true;
}

