#include <cmath>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "game/runtime/shared/vfx/tail_fire/SharedTailFireAnchorMath.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool approx(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

bool approxVec3(const glm::vec3& a, const glm::vec3& b, float eps = 0.0001f) {
    return approx(a.x, b.x, eps) &&
           approx(a.y, b.y, eps) &&
           approx(a.z, b.z, eps);
}

} // namespace

bool test_shared_tail_fire_anchor_math_contract(std::string& outFail) {
    using namespace game::runtime::shared_tail_fire_anchor_math;

    if (!expect(approxVec3(safeNormOr(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                           glm::vec3(0.0f, 1.0f, 0.0f)),
                "safeNormOr should return the fallback vector for a near-zero input.",
                outFail)) {
        return false;
    }

    const glm::mat4 baseWorld =
        glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    const glm::mat4 tipWorld =
        glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 4.0f, 3.0f));
    const AnchorFrame exactFrame =
        buildExactFireAnchorFrame(baseWorld, tipWorld, glm::vec3(0.0f, 0.0f, 1.0f));

    if (!expect(approxVec3(exactFrame.posWorld, glm::vec3(1.0f, 2.0f, 3.0f)) &&
                    approxVec3(exactFrame.tipPosWorld, glm::vec3(1.0f, 4.0f, 3.0f)),
                "buildExactFireAnchorFrame should preserve the authored base and tip positions.",
                outFail)) {
        return false;
    }
    if (!expect(approxVec3(glm::vec3(exactFrame.basis[1]), glm::vec3(0.0f, 1.0f, 0.0f)) &&
                    approxVec3(exactFrame.backDirWorld, glm::vec3(0.0f, 0.0f, 1.0f)),
                "buildExactFireAnchorFrame should align the up axis to the fire rig and rotate the local back direction into world space.",
                outFail)) {
        return false;
    }

    glm::mat4 tailWorld(1.0f);
    tailWorld[0] = glm::vec4(0.0f, 0.0f, 2.0f, 0.0f);
    tailWorld[1] = glm::vec4(0.0f, 3.0f, 0.0f, 0.0f);
    tailWorld[2] = glm::vec4(-4.0f, 0.0f, 0.0f, 0.0f);
    tailWorld[3] = glm::vec4(-2.0f, 5.0f, 7.0f, 1.0f);
    const AnchorFrame tailFrame =
        buildTailTipAnchorFrame(tailWorld, glm::vec3(1.0f, 0.0f, 0.0f));

    if (!expect(approxVec3(tailFrame.posWorld, glm::vec3(-2.0f, 5.0f, 7.0f)) &&
                    approxVec3(tailFrame.tipPosWorld, tailFrame.posWorld),
                "buildTailTipAnchorFrame should expose the tail-tip translation as the anchor position.",
                outFail)) {
        return false;
    }

    const glm::vec3 xAxis = glm::vec3(tailFrame.basis[0]);
    const glm::vec3 yAxis = glm::vec3(tailFrame.basis[1]);
    const glm::vec3 zAxis = glm::vec3(tailFrame.basis[2]);
    if (!expect(approx(glm::length(xAxis), 1.0f) &&
                    approx(glm::length(yAxis), 1.0f) &&
                    approx(glm::length(zAxis), 1.0f) &&
                    approx(glm::dot(xAxis, yAxis), 0.0f) &&
                    approx(glm::dot(xAxis, zAxis), 0.0f) &&
                    approx(glm::dot(yAxis, zAxis), 0.0f),
                "buildTailTipAnchorFrame should remove scale and keep an orthonormal basis for particle emission.",
                outFail)) {
        return false;
    }
    if (!expect(approxVec3(tailFrame.backDirWorld, xAxis),
                "buildTailTipAnchorFrame should rotate the local back direction through the cleaned tail basis.",
                outFail)) {
        return false;
    }

    return true;
}
