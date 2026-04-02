#include "game/world/MoveImpactMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "game/runtime/shared/backend/SharedBackendPoseEval.h"

namespace {

glm::vec3 safeForwardXZLocal(const glm::vec3& value) {
    glm::vec3 forward(value.x, 0.0f, value.z);
    const float lenSq = glm::dot(forward, forward);
    if (lenSq <= 0.000001f) {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return forward / std::sqrt(lenSq);
}

glm::mat4 buildModelInstanceTransformLocal(const PokemonInstance& instance,
                                           float backendModelScaleFactor = 1.0f) {
    const float modelScale =
        computeModelWorldScaleForMoveImpact(instance, backendModelScaleFactor);
    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(modelScale));
    const glm::mat4 rotationX =
        glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
    const glm::mat4 rotationY =
        glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
    const glm::mat4 rotationZ =
        glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
    const glm::mat4 translation =
        glm::translate(glm::mat4(1.0f), computeMoveImpactRenderOrigin(instance));
    return translation * rotationY * rotationX * rotationZ * scale;
}

glm::vec3 closestPointOnTriangleLocal(const glm::vec3& point,
                                      const glm::vec3& a,
                                      const glm::vec3& b,
                                      const glm::vec3& c) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = point - a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    const glm::vec3 bp = point - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / std::max(0.000001f, d1 - d3);
        return a + ab * v;
    }

    const glm::vec3 cp = point - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / std::max(0.000001f, d2 - d6);
        return a + ac * w;
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const glm::vec3 bc = c - b;
        const float w = (d4 - d3) / std::max(0.000001f, (d4 - d3) + (d5 - d6));
        return b + bc * w;
    }

    const float denom = 1.0f / std::max(0.000001f, va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    return a + ab * v + ac * w;
}

const glm::mat4& nodeGlobalForLocal(
    const game::runtime::render_model::MeshData& mesh,
    const game::runtime::shared_backend_pose::PoseEval* pose,
    int nodeIndex) {
    static const glm::mat4 kIdentity(1.0f);
    if (nodeIndex >= 0 && pose &&
        static_cast<std::size_t>(nodeIndex) < pose->nodeGlobals.size()) {
        return pose->nodeGlobals[static_cast<std::size_t>(nodeIndex)];
    }
    if (nodeIndex >= 0 &&
        static_cast<std::size_t>(nodeIndex) < mesh.bindNodeGlobals.size()) {
        return mesh.bindNodeGlobals[static_cast<std::size_t>(nodeIndex)];
    }
    return kIdentity;
}

int triangleNodeFallbackLocal(const game::runtime::render_model::MeshData& mesh,
                              std::size_t triangleIndex) {
    if (triangleIndex >= mesh.triangleSubmesh.size()) return -1;
    const std::uint16_t submesh = mesh.triangleSubmesh[triangleIndex];
    if (static_cast<std::size_t>(submesh) >= mesh.submeshMeshIndex.size()) return -1;
    const int meshIndex = mesh.submeshMeshIndex[static_cast<std::size_t>(submesh)];
    if (meshIndex < 0 || static_cast<std::size_t>(meshIndex) >= mesh.meshIndexToNode.size()) {
        return -1;
    }
    return mesh.meshIndexToNode[static_cast<std::size_t>(meshIndex)];
}

glm::vec3 resolveWorldVertexLocal(
    const game::runtime::render_model::MeshData& mesh,
    const game::runtime::shared_backend_pose::PoseEval* pose,
    const glm::mat4& modelMatrix,
    const std::vector<std::vector<glm::mat4>>& skinMatricesBySkin,
    const std::vector<std::uint8_t>& skinMatricesReady,
    std::uint32_t vertexIndex,
    int triNodeIndex,
    int triSkinIndex) {
    if (vertexIndex >= mesh.vertices.size()) return glm::vec3(0.0f);
    const auto& vertex = mesh.vertices[vertexIndex];

    if (triSkinIndex >= 0 &&
        static_cast<std::size_t>(triSkinIndex) < mesh.skins.size() &&
        static_cast<std::size_t>(triSkinIndex) < skinMatricesReady.size() &&
        skinMatricesReady[static_cast<std::size_t>(triSkinIndex)] != 0u) {
        const auto& mats = skinMatricesBySkin[static_cast<std::size_t>(triSkinIndex)];
        const std::array<std::uint16_t, 4> joints = {vertex.j0, vertex.j1, vertex.j2, vertex.j3};
        const std::array<float, 4> weights = {vertex.w0, vertex.w1, vertex.w2, vertex.w3};
        glm::vec4 blendedPos(0.0f);
        float totalWeight = 0.0f;
        for (std::size_t i = 0; i < joints.size(); ++i) {
            const float weight = weights[i];
            if (weight <= 0.00001f) continue;
            const std::size_t joint = static_cast<std::size_t>(joints[i]);
            if (joint >= mats.size()) continue;
            blendedPos += (mats[joint] * glm::vec4(vertex.position, 1.0f)) * weight;
            totalWeight += weight;
        }
        if (totalWeight > 0.00001f) {
            return glm::vec3(modelMatrix * blendedPos);
        }
    }

    if (triNodeIndex >= 0) {
        return glm::vec3(
            modelMatrix *
            nodeGlobalForLocal(mesh, pose, triNodeIndex) *
            glm::vec4(vertex.position, 1.0f));
    }
    return glm::vec3(modelMatrix * glm::vec4(vertex.position, 1.0f));
}

void prepareSkinMatricesLocal(
    const game::runtime::render_model::MeshData& mesh,
    const game::runtime::shared_backend_pose::PoseEval* pose,
    std::vector<std::vector<glm::mat4>>& outSkinMatricesBySkin,
    std::vector<std::uint8_t>& outReady) {
    outSkinMatricesBySkin.assign(mesh.skins.size(), {});
    outReady.assign(mesh.skins.size(), 0u);
    for (std::size_t skinIndex = 0; skinIndex < mesh.skins.size(); ++skinIndex) {
        const auto& skin = mesh.skins[skinIndex];
        if (skin.joints.empty()) continue;
        auto& mats = outSkinMatricesBySkin[skinIndex];
        mats.assign(skin.joints.size(), glm::mat4(1.0f));
        for (std::size_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex) {
            const int jointNode = skin.joints[jointIndex];
            const glm::mat4& jointGlobal = nodeGlobalForLocal(mesh, pose, jointNode);
            const glm::mat4 inverseBind =
                (jointIndex < skin.inverseBind.size()) ? skin.inverseBind[jointIndex] : glm::mat4(1.0f);
            mats[jointIndex] = jointGlobal * inverseBind;
        }
        outReady[skinIndex] = 1u;
    }
}

float pointScoreLocal(const glm::vec3& candidate, const glm::vec3& probe) {
    const glm::vec2 planarDelta(candidate.x - probe.x, candidate.z - probe.z);
    const float planarDistSq = glm::dot(planarDelta, planarDelta);
    const float verticalDelta = candidate.y - probe.y;
    constexpr float kVerticalWeight = 2.25f;
    return planarDistSq + (verticalDelta * verticalDelta * kVerticalWeight * kVerticalWeight);
}

MoveImpactSurfacePoint computeFallbackSurfacePointLocal(
    const PokemonInstance& target,
    const PokemonInstance* attacker,
    const game::runtime::render_model::MeshData* targetMesh,
    const game::runtime::render_model::MeshData* attackerMesh) {
    MoveImpactSurfacePoint out;
    const glm::vec3 targetCenter = computeMoveImpactWorldCenter(target, targetMesh);
    const glm::vec3 attackerCenter =
        attacker ? computeMoveImpactWorldCenter(*attacker, attackerMesh) : targetCenter;
    const glm::vec3 towardAttacker = safeForwardXZLocal(attackerCenter - targetCenter);
    out.forward = safeForwardXZLocal(targetCenter - attackerCenter);

    float horizontalRadius = 0.0f;
    float verticalHalf = 0.0f;
    if (targetMesh) {
        const float scale = computeModelWorldScaleForMoveImpact(target, targetMesh->modelScaleFactor);
        const glm::vec3 extents = (targetMesh->boundsMax - targetMesh->boundsMin) * 0.5f;
        horizontalRadius = std::max(std::abs(extents.x), std::abs(extents.z)) * scale;
        verticalHalf = std::abs(extents.y) * scale;
    } else if (target.model && target.model->hasBounds()) {
        const float scale = computeModelWorldScaleForMoveImpact(target);
        const glm::vec3 extents = (target.model->getBoundsMax() - target.model->getBoundsMin()) * 0.5f;
        horizontalRadius = std::max(std::abs(extents.x), std::abs(extents.z)) * scale;
        verticalHalf = std::abs(extents.y) * scale;
    }

    out.position = targetCenter + towardAttacker * horizontalRadius;
    out.position.y = std::clamp(targetCenter.y,
                                targetCenter.y - verticalHalf,
                                targetCenter.y + verticalHalf);
    out.normal = towardAttacker;
    return out;
}

} // namespace

glm::vec3 computeMoveImpactRenderOrigin(const PokemonInstance& instance) {
    return instance.position + glm::vec3(0.0f, instance.visualYOffset, 0.0f);
}

glm::vec3 computeMoveImpactWorldCenter(
    const PokemonInstance& instance,
    const game::runtime::render_model::MeshData* mesh,
    float backendModelScaleFactor) {
    if (mesh) {
        const glm::vec3 localCenter = (mesh->boundsMin + mesh->boundsMax) * 0.5f;
        const glm::mat4 modelMatrix =
            buildModelInstanceTransformLocal(instance, std::max(0.01f, mesh->modelScaleFactor));
        return glm::vec3(modelMatrix * glm::vec4(localCenter, 1.0f));
    }

    if (instance.model && instance.model->hasBounds()) {
        const glm::vec3 localCenter =
            (instance.model->getBoundsMin() + instance.model->getBoundsMax()) * 0.5f;
        const glm::mat4 modelMatrix =
            buildModelInstanceTransformLocal(instance, backendModelScaleFactor);
        return glm::vec3(modelMatrix * glm::vec4(localCenter, 1.0f));
    }

    return computeMoveImpactRenderOrigin(instance);
}

MoveImpactSurfacePoint computeTargetSurfaceImpactPoint(
    const PokemonInstance& target,
    const PokemonInstance* attacker,
    const game::runtime::render_model::MeshData* targetMesh,
    const game::runtime::render_model::MeshData* attackerMesh) {
    if (!targetMesh || targetMesh->indices.size() < 3u || targetMesh->vertices.empty()) {
        return computeFallbackSurfacePointLocal(target, attacker, targetMesh, attackerMesh);
    }

    const glm::vec3 targetCenter = computeMoveImpactWorldCenter(target, targetMesh);
    const glm::vec3 attackerCenter =
        attacker ? computeMoveImpactWorldCenter(*attacker, attackerMesh) : targetCenter;
    const glm::vec3 probe = attacker ? attackerCenter : targetCenter;

    game::runtime::shared_backend_pose::PoseEval targetPose;
    if (!targetMesh->nodesDefault.empty()) {
        game::runtime::shared_backend_pose::evaluateScenePose(*targetMesh, target, targetPose);
    }
    const auto* pose = targetPose.hasScenePose ? &targetPose : nullptr;

    std::vector<std::vector<glm::mat4>> skinMatricesBySkin;
    std::vector<std::uint8_t> skinMatricesReady;
    prepareSkinMatricesLocal(*targetMesh, pose, skinMatricesBySkin, skinMatricesReady);

    const glm::mat4 targetModelMatrix =
        buildModelInstanceTransformLocal(target, std::max(0.01f, targetMesh->modelScaleFactor));

    MoveImpactSurfacePoint best =
        computeFallbackSurfacePointLocal(target, attacker, targetMesh, attackerMesh);
    float bestScore = std::numeric_limits<float>::max();
    bool foundSurface = false;

    const std::size_t triangleCount = targetMesh->indices.size() / 3u;
    for (std::size_t triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex) {
        const std::size_t indexBase = triangleIndex * 3u;
        const std::uint32_t i0 = targetMesh->indices[indexBase + 0u];
        const std::uint32_t i1 = targetMesh->indices[indexBase + 1u];
        const std::uint32_t i2 = targetMesh->indices[indexBase + 2u];
        if (i0 >= targetMesh->vertices.size() ||
            i1 >= targetMesh->vertices.size() ||
            i2 >= targetMesh->vertices.size()) {
            continue;
        }

        int triNodeIndex =
            (triangleIndex < targetMesh->triangleNodeIndex.size())
                ? targetMesh->triangleNodeIndex[triangleIndex]
                : -1;
        if (triNodeIndex < 0) {
            triNodeIndex = triangleNodeFallbackLocal(*targetMesh, triangleIndex);
        }
        const int triSkinIndex =
            (triangleIndex < targetMesh->triangleSkinIndex.size())
                ? targetMesh->triangleSkinIndex[triangleIndex]
                : -1;

        const glm::vec3 a = resolveWorldVertexLocal(
            *targetMesh, pose, targetModelMatrix, skinMatricesBySkin, skinMatricesReady, i0, triNodeIndex, triSkinIndex);
        const glm::vec3 b = resolveWorldVertexLocal(
            *targetMesh, pose, targetModelMatrix, skinMatricesBySkin, skinMatricesReady, i1, triNodeIndex, triSkinIndex);
        const glm::vec3 c = resolveWorldVertexLocal(
            *targetMesh, pose, targetModelMatrix, skinMatricesBySkin, skinMatricesReady, i2, triNodeIndex, triSkinIndex);

        const glm::vec3 nearest = closestPointOnTriangleLocal(probe, a, b, c);
        const float score = pointScoreLocal(nearest, probe);
        if (!(score < bestScore)) continue;

        glm::vec3 normal = glm::cross(b - a, c - a);
        const float normalLenSq = glm::dot(normal, normal);
        if (normalLenSq > 0.000001f) {
            normal /= std::sqrt(normalLenSq);
        } else {
            normal = safeForwardXZLocal(attackerCenter - targetCenter);
        }
        if (attacker && glm::dot(normal, attackerCenter - nearest) < 0.0f) {
            normal = -normal;
        }

        bestScore = score;
        best.position = nearest;
        best.normal = normal;
        best.usedMeshSurface = true;
        foundSurface = true;
    }

    if (!foundSurface) {
        return best;
    }

    float verticalHalf = 0.0f;
    if (targetMesh) {
        const float scale = computeModelWorldScaleForMoveImpact(target, targetMesh->modelScaleFactor);
        const glm::vec3 extents = (targetMesh->boundsMax - targetMesh->boundsMin) * 0.5f;
        verticalHalf = std::abs(extents.y) * scale;
    } else if (target.model && target.model->hasBounds()) {
        const float scale = computeModelWorldScaleForMoveImpact(target);
        const glm::vec3 extents = (target.model->getBoundsMax() - target.model->getBoundsMin()) * 0.5f;
        verticalHalf = std::abs(extents.y) * scale;
    }
    best.position.y = std::clamp(targetCenter.y,
                                 targetCenter.y - verticalHalf,
                                 targetCenter.y + verticalHalf);
    best.forward = attacker ? safeForwardXZLocal(best.position - attackerCenter)
                            : safeForwardXZLocal(targetCenter - best.position);
    return best;
}
