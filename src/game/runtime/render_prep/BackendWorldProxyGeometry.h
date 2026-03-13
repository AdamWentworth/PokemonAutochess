#pragma once

#include "game/PokemonInstance.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace game::runtime::backend_proxy {

struct UnitProxyExtents {
    float halfWidth = 0.12f;
    float halfDepth = 0.12f;
    float height = 0.42f;
};

inline UnitProxyExtents computeUnitProxyExtents(const PokemonInstance& unit, float worldCellSize) {
    const float cell = std::max(0.05f, worldCellSize);
    const bool allowFadeToZero = unit.fainting || !unit.alive;
    const float visualScale = allowFadeToZero
        ? std::max(0.0f, unit.visualScale)
        : std::max(0.55f, unit.visualScale);
    const float captureScale = allowFadeToZero
        ? std::max(0.0f, unit.captureScale)
        : std::max(0.55f, unit.captureScale);
    const float scale =
        std::max(0.55f, unit.speciesScale) *
        std::max(0.40f, unit.modelScaleCorrection) *
        visualScale *
        captureScale;

    if (allowFadeToZero && scale <= 0.0001f) {
        UnitProxyExtents out;
        out.halfWidth = 0.0f;
        out.halfDepth = 0.0f;
        out.height = 0.0f;
        return out;
    }

    UnitProxyExtents out;
    const float baseRadius = cell * 0.22f;
    out.halfWidth = std::clamp(baseRadius * scale, cell * 0.08f, cell * 0.48f);
    out.halfDepth = std::clamp(baseRadius * scale * 0.95f, cell * 0.08f, cell * 0.48f);
    out.height = std::clamp(cell * 0.72f * scale, cell * 0.20f, cell * 2.20f);
    return out;
}

inline glm::vec3 yawForward(float yawDegrees) {
    const float yawRad = glm::radians(yawDegrees);
    return glm::normalize(glm::vec3(std::sin(yawRad), 0.0f, std::cos(yawRad)));
}

inline glm::vec3 yawRight(float yawDegrees) {
    const float yawRad = glm::radians(yawDegrees);
    return glm::normalize(glm::vec3(std::cos(yawRad), 0.0f, -std::sin(yawRad)));
}

struct UnitProxyCorners {
    std::array<glm::vec3, 4> bottom;
    std::array<glm::vec3, 4> top;
    glm::vec3 center{0.0f};
};

inline UnitProxyCorners computeUnitProxyCorners(const glm::vec3& center,
                                                const UnitProxyExtents& extents,
                                                float yawDegrees,
                                                float groundYBias = 0.02f) {
    const glm::vec3 right = yawRight(yawDegrees) * extents.halfWidth;
    const glm::vec3 forward = yawForward(yawDegrees) * extents.halfDepth;
    const glm::vec3 baseCenter = center + glm::vec3(0.0f, groundYBias, 0.0f);
    const glm::vec3 lift(0.0f, extents.height, 0.0f);

    UnitProxyCorners out;
    out.center = baseCenter + lift * 0.5f;
    out.bottom[0] = baseCenter - right - forward;
    out.bottom[1] = baseCenter + right - forward;
    out.bottom[2] = baseCenter + right + forward;
    out.bottom[3] = baseCenter - right + forward;
    out.top[0] = out.bottom[0] + lift;
    out.top[1] = out.bottom[1] + lift;
    out.top[2] = out.bottom[2] + lift;
    out.top[3] = out.bottom[3] + lift;
    return out;
}

inline std::array<glm::vec3, 4> computeShadowQuad(const glm::vec3& center,
                                                  float halfWidth,
                                                  float halfDepth,
                                                  float yawDegrees,
                                                  float y = 0.012f) {
    const glm::vec3 right = yawRight(yawDegrees) * halfWidth;
    const glm::vec3 forward = yawForward(yawDegrees) * halfDepth;
    const glm::vec3 c(center.x, y, center.z);
    return {
        c - right - forward,
        c + right - forward,
        c + right + forward,
        c - right + forward
    };
}

} // namespace game::runtime::backend_proxy
