#pragma once

#include <glm/glm.hpp>

namespace game::runtime::shared_tail_fire_anchor_math {

struct AnchorFrame {
    glm::vec3 posWorld{0.0f};
    glm::vec3 tipPosWorld{0.0f};
    glm::mat3 basis{1.0f};
    glm::vec3 backDirWorld{0.0f, 1.0f, 0.0f};
};

glm::vec3 safeNormOr(glm::vec3 v, const glm::vec3& fallback);
glm::mat3 orthonormalBasis(const glm::mat4& worldMatrix);
glm::mat3 buildFireAnchorBasis(const glm::mat4& baseWorldMatrix,
                               const glm::vec3& basePosWorld,
                               const glm::vec3& tipPosWorld);
glm::vec3 rotateBackDir(const glm::mat3& basis, const glm::vec3& localBackDir);
AnchorFrame buildExactFireAnchorFrame(const glm::mat4& baseWorldMatrix,
                                      const glm::mat4& tipWorldMatrix,
                                      const glm::vec3& localBackDir);
AnchorFrame buildTailTipAnchorFrame(const glm::mat4& tailWorldMatrix,
                                    const glm::vec3& localBackDir);

} // namespace game::runtime::shared_tail_fire_anchor_math
