#include "game/runtime/shared/backend/SharedBackendPoseEval.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace game::runtime::shared_backend_pose {
namespace {

glm::quat normalizeQuatIfNeeded(const glm::quat& q) {
    const float lenSq = glm::dot(q, q);
    if (lenSq <= 0.0f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (std::abs(lenSq - 1.0f) <= 0.0001f) return q;
    return q * glm::inversesqrt(lenSq);
}

glm::mat4 trsToMat4(const pac_model_types::NodeTRS& n) {
    if (n.hasMatrix) return n.matrix;
    const glm::mat4 t = glm::translate(glm::mat4(1.0f), n.t);
    const glm::mat4 r = glm::mat4_cast(normalizeQuatIfNeeded(n.r));
    const glm::mat4 s = glm::scale(glm::mat4(1.0f), n.s);
    return t * r * s;
}

float wrapTime(float t, float duration) {
    if (duration <= 0.0f) return 0.0f;
    float wrapped = std::fmod(t, duration);
    if (wrapped < 0.0f) wrapped += duration;
    return wrapped;
}

std::size_t findKeyframe(const std::vector<float>& times, float t) {
    if (times.empty()) return 0u;
    if (t <= times.front()) return 0u;
    if (t >= times.back()) return times.size() - 1u;
    const auto it = std::upper_bound(times.begin(), times.end(), t);
    return (it == times.begin()) ? 0u : static_cast<std::size_t>((it - times.begin()) - 1u);
}

glm::vec4 sampleVec4(const pac_model_types::AnimationSampler& sampler, float t) {
    if (sampler.inputs.empty() || sampler.outputs.empty()) return glm::vec4(0.0f);
    const std::size_t i = findKeyframe(sampler.inputs, t);
    if (i >= sampler.inputs.size() - 1u) {
        return sampler.outputs[std::min(i, sampler.outputs.size() - 1u)];
    }
    const float t0 = sampler.inputs[i];
    const float t1 = sampler.inputs[i + 1u];
    const float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
    const glm::vec4 v0 = sampler.outputs[std::min(i, sampler.outputs.size() - 1u)];
    const glm::vec4 v1 = sampler.outputs[std::min(i + 1u, sampler.outputs.size() - 1u)];
    if (sampler.interpolation == "STEP") return v0;
    return glm::mix(v0, v1, std::clamp(a, 0.0f, 1.0f));
}

struct ScenePoseMeshCache {
    std::vector<int> evalOrder;
    std::vector<glm::mat4> defaultLocalMatrices;
    std::vector<std::vector<std::uint8_t>> animatedNodeMaskByClip;
    std::vector<std::vector<int>> animatedNodesByClip;
};

const ScenePoseMeshCache& scenePoseMeshCacheFor(const backend_model::MeshData& mesh) {
    static std::unordered_map<const backend_model::MeshData*, ScenePoseMeshCache> cache;
    const auto found = cache.find(&mesh);
    if (found != cache.end()) {
        return found->second;
    }

    ScenePoseMeshCache built;
    const std::size_t nodeCount = mesh.nodesDefault.size();
    built.defaultLocalMatrices.reserve(nodeCount);
    for (const auto& node : mesh.nodesDefault) {
        built.defaultLocalMatrices.push_back(trsToMat4(node));
    }

    built.evalOrder.reserve(nodeCount);
    std::vector<std::uint8_t> visited(nodeCount, 0u);
    std::vector<int> stack;
    stack.reserve(nodeCount);
    const auto visitRoot = [&](int root) {
        if (root < 0 || static_cast<std::size_t>(root) >= nodeCount) return;
        stack.clear();
        stack.push_back(root);
        while (!stack.empty()) {
            const int node = stack.back();
            stack.pop_back();
            if (node < 0 || static_cast<std::size_t>(node) >= nodeCount) continue;
            if (visited[static_cast<std::size_t>(node)] != 0u) continue;
            visited[static_cast<std::size_t>(node)] = 1u;
            built.evalOrder.push_back(node);
            if (static_cast<std::size_t>(node) >= mesh.nodeChildren.size()) continue;
            const auto& children = mesh.nodeChildren[static_cast<std::size_t>(node)];
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push_back(*it);
            }
        }
    };

    if (!mesh.sceneRoots.empty()) {
        for (const int root : mesh.sceneRoots) {
            visitRoot(root);
        }
    } else {
        bool visitedAnyRoot = false;
        for (std::size_t i = 0; i < mesh.nodeParent.size() && i < nodeCount; ++i) {
            if (mesh.nodeParent[i] >= 0) continue;
            visitRoot(static_cast<int>(i));
            visitedAnyRoot = true;
        }
        if (!visitedAnyRoot && nodeCount > 0u) {
            visitRoot(0);
        }
    }
    for (std::size_t i = 0; i < nodeCount; ++i) {
        if (visited[i] == 0u) {
            visitRoot(static_cast<int>(i));
        }
    }

    built.animatedNodeMaskByClip.resize(mesh.animations.size());
    built.animatedNodesByClip.resize(mesh.animations.size());
    for (std::size_t clipIndex = 0; clipIndex < mesh.animations.size(); ++clipIndex) {
        auto& mask = built.animatedNodeMaskByClip[clipIndex];
        auto& animatedNodes = built.animatedNodesByClip[clipIndex];
        mask.assign(nodeCount, 0u);
        for (const auto& channel : mesh.animations[clipIndex].channels) {
            if (channel.targetNode < 0) continue;
            const std::size_t nodeIndex = static_cast<std::size_t>(channel.targetNode);
            if (nodeIndex >= nodeCount) continue;
            if (mask[nodeIndex] == 0u) {
                mask[nodeIndex] = 1u;
                animatedNodes.push_back(channel.targetNode);
            }
        }
    }

    const auto inserted = cache.emplace(&mesh, std::move(built));
    return inserted.first->second;
}

const std::vector<std::uint8_t>& rootMotionCarrierMaskForMesh(const backend_model::MeshData& mesh) {
    static std::unordered_map<const backend_model::MeshData*, std::vector<std::uint8_t>> cache;
    const auto found = cache.find(&mesh);
    if (found != cache.end()) {
        return found->second;
    }

    std::vector<std::uint8_t> mask(mesh.nodesDefault.size(), 0u);
    for (const auto& skin : mesh.skins) {
        if (skin.joints.empty()) continue;
        std::unordered_set<int> jointSet;
        jointSet.reserve(skin.joints.size());
        for (const int jointNode : skin.joints) {
            jointSet.insert(jointNode);
        }
        for (const int jointNode : skin.joints) {
            if (jointNode < 0 ||
                static_cast<std::size_t>(jointNode) >= mesh.nodeParent.size() ||
                static_cast<std::size_t>(jointNode) >= mask.size()) {
                continue;
            }
            const int parent = mesh.nodeParent[static_cast<std::size_t>(jointNode)];
            if (parent < 0 || jointSet.find(parent) == jointSet.end()) {
                mask[static_cast<std::size_t>(jointNode)] = 1u;
            }
        }
    }

    const auto inserted = cache.emplace(&mesh, std::move(mask));
    return inserted.first->second;
}

void buildGlobals(const backend_model::MeshData& mesh, PoseEval& eval, int animIndex) {
    const auto& meshCache = scenePoseMeshCacheFor(mesh);
    const std::vector<std::uint8_t>* animatedNodeMask = nullptr;
    if (animIndex >= 0 && static_cast<std::size_t>(animIndex) < meshCache.animatedNodeMaskByClip.size()) {
        animatedNodeMask = &meshCache.animatedNodeMaskByClip[static_cast<std::size_t>(animIndex)];
    }

    for (const int node : meshCache.evalOrder) {
        if (node < 0 || static_cast<std::size_t>(node) >= eval.nodeGlobals.size()) continue;
        const std::size_t nodeIndex = static_cast<std::size_t>(node);
        const glm::mat4 localM =
            (animatedNodeMask && (*animatedNodeMask)[nodeIndex] != 0u)
                ? trsToMat4(eval.nodeLocals[nodeIndex])
                : meshCache.defaultLocalMatrices[nodeIndex];
        const int parent =
            (nodeIndex < mesh.nodeParent.size()) ? mesh.nodeParent[nodeIndex] : -1;
        if (parent >= 0 && static_cast<std::size_t>(parent) < eval.nodeGlobals.size()) {
            eval.nodeGlobals[nodeIndex] =
                eval.nodeGlobals[static_cast<std::size_t>(parent)] * localM;
        } else {
            eval.nodeGlobals[nodeIndex] = localM;
        }
    }
}

void applyClipPose(const backend_model::MeshData& mesh,
                  PoseEval& eval,
                  int animIndex,
                  float animTimeSec,
                  bool preserveRootMotionCarrierXZ) {
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) return;
    const auto& clip = mesh.animations[static_cast<std::size_t>(animIndex)];
    const auto& meshCache = scenePoseMeshCacheFor(mesh);
    const float clipTime = wrapTime(animTimeSec, clip.durationSec);
    const std::vector<std::uint8_t>* rootMask = nullptr;
    if (preserveRootMotionCarrierXZ) {
        rootMask = &rootMotionCarrierMaskForMesh(mesh);
    }
    const auto& animatedNodes = meshCache.animatedNodesByClip[static_cast<std::size_t>(animIndex)];
    for (const int nodeIndex : animatedNodes) {
        if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= eval.nodeLocals.size()) continue;
        eval.nodeLocals[static_cast<std::size_t>(nodeIndex)] =
            mesh.nodesDefault[static_cast<std::size_t>(nodeIndex)];
    }

    thread_local std::vector<glm::vec4> sampledVec4BySampler;
    thread_local std::vector<glm::quat> sampledQuatBySampler;
    thread_local std::vector<std::uint8_t> sampledVec4ReadyBySampler;
    thread_local std::vector<std::uint8_t> sampledQuatReadyBySampler;
    const std::size_t samplerCount = clip.samplers.size();
    if (sampledVec4BySampler.size() < samplerCount) {
        sampledVec4BySampler.resize(samplerCount);
    }
    if (sampledQuatBySampler.size() < samplerCount) {
        sampledQuatBySampler.resize(samplerCount);
    }
    if (sampledVec4ReadyBySampler.size() < samplerCount) {
        sampledVec4ReadyBySampler.resize(samplerCount, 0u);
    }
    if (sampledQuatReadyBySampler.size() < samplerCount) {
        sampledQuatReadyBySampler.resize(samplerCount, 0u);
    }
    if (samplerCount > 0u) {
        std::fill(sampledVec4ReadyBySampler.begin(), sampledVec4ReadyBySampler.begin() + samplerCount, 0u);
        std::fill(sampledQuatReadyBySampler.begin(), sampledQuatReadyBySampler.begin() + samplerCount, 0u);
    }

    for (const auto& channel : clip.channels) {
        if (channel.targetNode < 0 || static_cast<std::size_t>(channel.targetNode) >= eval.nodeLocals.size()) {
            continue;
        }
        if (channel.samplerIndex < 0 || static_cast<std::size_t>(channel.samplerIndex) >= clip.samplers.size()) {
            continue;
        }
        auto& local = eval.nodeLocals[static_cast<std::size_t>(channel.targetNode)];
        const std::size_t samplerIndex = static_cast<std::size_t>(channel.samplerIndex);
        const auto& sampler = clip.samplers[samplerIndex];
        if (channel.path == pac_model_types::ChannelPath::Translation) {
            if (sampledVec4ReadyBySampler[samplerIndex] == 0u) {
                sampledVec4BySampler[samplerIndex] = sampleVec4(sampler, clipTime);
                sampledVec4ReadyBySampler[samplerIndex] = 1u;
            }
            const glm::vec4 tr = sampledVec4BySampler[samplerIndex];
            const bool rootMotionCarrier =
                preserveRootMotionCarrierXZ &&
                rootMask &&
                (channel.targetNode >= 0) &&
                (static_cast<std::size_t>(channel.targetNode) < rootMask->size()) &&
                ((*rootMask)[static_cast<std::size_t>(channel.targetNode)] != 0u);
            if (rootMotionCarrier) {
                const auto& bind = mesh.nodesDefault[static_cast<std::size_t>(channel.targetNode)];
                if (bind.hasMatrix) {
                    local = bind;
                    local.matrix[3].x = bind.matrix[3].x;
                    local.matrix[3].y = tr.y;
                    local.matrix[3].z = bind.matrix[3].z;
                    local.matrix[3].w = 1.0f;
                    local.hasMatrix = true;
                } else {
                    local.t = glm::vec3(bind.t.x, tr.y, bind.t.z);
                    local.hasMatrix = false;
                }
            } else {
                local.t = glm::vec3(tr.x, tr.y, tr.z);
                local.hasMatrix = false;
            }
        } else if (channel.path == pac_model_types::ChannelPath::Scale) {
            if (sampledVec4ReadyBySampler[samplerIndex] == 0u) {
                sampledVec4BySampler[samplerIndex] = sampleVec4(sampler, clipTime);
                sampledVec4ReadyBySampler[samplerIndex] = 1u;
            }
            const glm::vec4 sc = sampledVec4BySampler[samplerIndex];
            local.s = glm::vec3(sc.x, sc.y, sc.z);
            local.hasMatrix = false;
        } else if (channel.path == pac_model_types::ChannelPath::Rotation) {
            if (sampledQuatReadyBySampler[samplerIndex] == 0u) {
                if (sampledVec4ReadyBySampler[samplerIndex] == 0u) {
                    sampledVec4BySampler[samplerIndex] = sampleVec4(sampler, clipTime);
                    sampledVec4ReadyBySampler[samplerIndex] = 1u;
                }
                const glm::vec4 v = sampledVec4BySampler[samplerIndex];
                sampledQuatBySampler[samplerIndex] =
                    normalizeQuatIfNeeded(glm::quat(v.w, v.x, v.y, v.z));
                sampledQuatReadyBySampler[samplerIndex] = 1u;
            }
            local.r = sampledQuatBySampler[samplerIndex];
            local.hasMatrix = false;
        }
    }
    eval.hasClipPose = true;
}

int resolveSceneAnimIndex(const backend_model::MeshData& mesh, const PokemonInstance& unit) {
    int animIndex = unit.activeAnimIndex;
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
        animIndex = unit.currentAttackAnimIndex;
    }
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
        animIndex = unit.animMoveIndex;
    }
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
        animIndex = unit.animIdleIndex;
    }
    if (animIndex < 0 && !mesh.animations.empty()) {
        animIndex = 0;
    }
    return animIndex;
}

void resetPoseEvalForMesh(const backend_model::MeshData& mesh, PoseEval& eval) {
    eval.hasScenePose = true;
    eval.hasClipPose = false;
    const std::size_t nodeCount = mesh.nodesDefault.size();

    if (eval.nodeLocals.size() != nodeCount) {
        eval.nodeLocals.resize(nodeCount);
    }

    if (eval.nodeGlobals.size() != nodeCount) {
        eval.nodeGlobals.resize(nodeCount);
    }
}

} // namespace

void evaluateScenePose(const backend_model::MeshData& mesh,
                       const PokemonInstance& unit,
                       PoseEval& outPose) {
    const int animIndex = resolveSceneAnimIndex(mesh, unit);
    evaluateScenePoseForResolvedClipTime(
        mesh,
        animIndex,
        unit.animTimeSec,
        true,
        outPose);
}

PoseEval evaluateScenePose(const backend_model::MeshData& mesh, const PokemonInstance& unit) {
    PoseEval eval;
    evaluateScenePose(mesh, unit, eval);
    return eval;
}

void evaluateScenePoseForResolvedClipTime(const backend_model::MeshData& mesh,
                                          int animIndex,
                                          float animTimeSec,
                                          bool preserveRootMotionCarrierXZ,
                                          PoseEval& outPose) {
    if (mesh.nodesDefault.empty()) {
        outPose = PoseEval{};
        return;
    }
    resetPoseEvalForMesh(mesh, outPose);

    if (animIndex >= 0) {
        applyClipPose(mesh, outPose, animIndex, animTimeSec, preserveRootMotionCarrierXZ);
    }

    buildGlobals(mesh, outPose, animIndex);
}

PoseEval evaluateScenePoseForResolvedClipTime(const backend_model::MeshData& mesh,
                                              int animIndex,
                                              float animTimeSec,
                                              bool preserveRootMotionCarrierXZ) {
    PoseEval eval;
    evaluateScenePoseForResolvedClipTime(
        mesh,
        animIndex,
        animTimeSec,
        preserveRootMotionCarrierXZ,
        eval);
    return eval;
}

void evaluateScenePoseForClipTime(const backend_model::MeshData& mesh,
                                  int animIndex,
                                  float animTimeSec,
                                  PoseEval& outPose) {
    evaluateScenePoseForResolvedClipTime(
        mesh,
        animIndex,
        animTimeSec,
        false,
        outPose);
}

PoseEval evaluateScenePoseForClipTime(const backend_model::MeshData& mesh,
                                      int animIndex,
                                      float animTimeSec) {
    return evaluateScenePoseForResolvedClipTime(
        mesh,
        animIndex,
        animTimeSec,
        false);
}

} // namespace game::runtime::shared_backend_pose
