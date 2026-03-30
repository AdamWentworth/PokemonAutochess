#include "game/runtime/shared/vfx/tail_fire/SharedTailFireAnchorMath.h"

#include <cmath>

namespace game::runtime::shared_tail_fire_anchor_math {

glm::vec3 safeNormOr(glm::vec3 v, const glm::vec3& fallback) {
    const float len2 = glm::dot(v, v);
    if (len2 <= 1e-10f) return fallback;
    return v * (1.0f / std::sqrt(len2));
}

glm::mat3 orthonormalBasis(const glm::mat4& worldMatrix) {
    glm::vec3 x = glm::vec3(worldMatrix[0]);
    glm::vec3 y = glm::vec3(worldMatrix[1]);

    x = safeNormOr(x, glm::vec3(1.0f, 0.0f, 0.0f));
    y = y - x * glm::dot(y, x);
    y = safeNormOr(y, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::vec3 z = safeNormOr(glm::cross(x, y), glm::vec3(0.0f, 0.0f, 1.0f));

    if (glm::dot(glm::cross(x, y), z) < 0.0f) {
        z = -z;
    }

    return glm::mat3(x, y, z);
}

glm::mat3 buildFireAnchorBasis(const glm::mat4& baseWorldMatrix,
                               const glm::vec3& basePosWorld,
                               const glm::vec3& tipPosWorld) {
    const glm::vec3 upAxis =
        safeNormOr(tipPosWorld - basePosWorld,
                   safeNormOr(glm::vec3(baseWorldMatrix[1]), glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 xHint = glm::vec3(baseWorldMatrix[0]);
    xHint -= upAxis * glm::dot(xHint, upAxis);
    if (glm::dot(xHint, xHint) <= 1e-10f) {
        xHint = glm::vec3(baseWorldMatrix[2]);
        xHint -= upAxis * glm::dot(xHint, upAxis);
    }

    const glm::vec3 xFallback = (std::fabs(upAxis.y) < 0.95f)
        ? safeNormOr(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), upAxis),
                     glm::vec3(1.0f, 0.0f, 0.0f))
        : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 xAxis = safeNormOr(xHint, xFallback);
    glm::vec3 zAxis = safeNormOr(glm::cross(xAxis, upAxis), glm::vec3(0.0f, 0.0f, 1.0f));
    xAxis = safeNormOr(glm::cross(upAxis, zAxis), xAxis);

    if (glm::dot(glm::cross(xAxis, upAxis), zAxis) < 0.0f) {
        zAxis = -zAxis;
    }

    return glm::mat3(xAxis, upAxis, zAxis);
}

glm::vec3 rotateBackDir(const glm::mat3& basis, const glm::vec3& localBackDir) {
    return safeNormOr(basis * localBackDir, glm::vec3(0.0f, 1.0f, 0.0f));
}

AnchorFrame buildExactFireAnchorFrame(const glm::mat4& baseWorldMatrix,
                                      const glm::mat4& tipWorldMatrix,
                                      const glm::vec3& localBackDir) {
    AnchorFrame out;
    out.posWorld = glm::vec3(baseWorldMatrix[3]);
    out.tipPosWorld = glm::vec3(tipWorldMatrix[3]);
    out.basis = buildFireAnchorBasis(baseWorldMatrix, out.posWorld, out.tipPosWorld);
    out.backDirWorld = rotateBackDir(out.basis, localBackDir);
    return out;
}

AnchorFrame buildTailTipAnchorFrame(const glm::mat4& tailWorldMatrix,
                                    const glm::vec3& localBackDir) {
    AnchorFrame out;
    out.posWorld = glm::vec3(tailWorldMatrix[3]);
    out.tipPosWorld = out.posWorld;
    out.basis = orthonormalBasis(tailWorldMatrix);
    out.backDirWorld = rotateBackDir(out.basis, localBackDir);
    return out;
}

} // namespace game::runtime::shared_tail_fire_anchor_math
