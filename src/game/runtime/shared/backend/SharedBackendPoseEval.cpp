#include "game/runtime/shared/backend/SharedBackendPoseEval.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "engine/runtime/FixedStep.h"

namespace game::runtime::shared_backend_pose {
namespace {

glm::quat normalizeQuatIfNeeded(const glm::quat& q) {
    const float lenSq = glm::dot(q, q);
    if (lenSq <= 0.0f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (std::abs(lenSq - 1.0f) <= 0.0001f) return q;
    return q * glm::inversesqrt(lenSq);
}

glm::mat4 trsToMat4(const engine::render::model_types::NodeTRS& n) {
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
    if (wrapped == 0.0f && t > 0.0f) {
        return std::nextafter(duration, 0.0f);
    }
    return wrapped;
}

std::size_t findKeyframe(const std::vector<float>& times, float t) {
    if (times.empty()) return 0u;
    if (t <= times.front()) return 0u;
    if (t >= times.back()) return times.size() - 1u;
    const auto it = std::upper_bound(times.begin(), times.end(), t);
    return (it == times.begin()) ? 0u : static_cast<std::size_t>((it - times.begin()) - 1u);
}

float sceneLoopClosureBlendWindowSec(float duration) {
    if (!(duration > 0.0f)) return 0.0f;
    constexpr float kDefaultWindowSec = engine::runtime::fixed_step::kSeconds;
    return std::min(kDefaultWindowSec, duration * 0.25f);
}

glm::vec4 sampleVec4NoLoop(const engine::render::model_types::AnimationSampler& sampler, float t) {
    if (sampler.inputs.empty() || sampler.outputs.empty()) return glm::vec4(0.0f);
    const auto valueAt = [&](std::size_t index) {
        return sampler.outputs[std::min(index, sampler.outputs.size() - 1u)];
    };
    if (sampler.inputs.size() == 1u || sampler.outputs.size() == 1u) {
        return valueAt(0u);
    }

    const float firstTime = sampler.inputs.front();
    const float lastTime = sampler.inputs.back();
    const glm::vec4 lastValue = valueAt(sampler.inputs.size() - 1u);
    if (t <= firstTime) return valueAt(0u);
    if (t >= lastTime) return lastValue;

    const std::size_t i = findKeyframe(sampler.inputs, t);
    if (i >= sampler.inputs.size() - 1u) return lastValue;
    const float t0 = sampler.inputs[i];
    const float t1 = sampler.inputs[i + 1u];
    const float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
    const glm::vec4 v0 = valueAt(i);
    const glm::vec4 v1 = valueAt(i + 1u);
    if (sampler.interpolation == "STEP") return v0;
    return glm::mix(v0, v1, std::clamp(a, 0.0f, 1.0f));
}

glm::vec4 sampleVec4LoopBase(const engine::render::model_types::AnimationSampler& sampler,
                             float t,
                             float duration) {
    if (sampler.inputs.empty() || sampler.outputs.empty()) return glm::vec4(0.0f);
    const auto valueAt = [&](std::size_t index) {
        return sampler.outputs[std::min(index, sampler.outputs.size() - 1u)];
    };
    if (sampler.inputs.size() == 1u || sampler.outputs.size() == 1u) {
        return valueAt(0u);
    }

    const float firstTime = sampler.inputs.front();
    const float lastTime = sampler.inputs.back();
    const glm::vec4 firstValue = valueAt(0u);
    const glm::vec4 lastValue = valueAt(sampler.inputs.size() - 1u);
    const bool outsideKeyRange = t < firstTime || t > lastTime;
    if (outsideKeyRange) {
        if (sampler.interpolation == "STEP") return lastValue;
        const float wrappedSpan = (duration - lastTime) + firstTime;
        if (!(wrappedSpan > 0.0f)) return lastValue;
        const float wrappedT =
            (t >= lastTime) ? (t - lastTime) : (t + duration - lastTime);
        const float a = std::clamp(wrappedT / wrappedSpan, 0.0f, 1.0f);
        return glm::mix(lastValue, firstValue, a);
    }

    const std::size_t i = findKeyframe(sampler.inputs, t);
    if (i >= sampler.inputs.size() - 1u) {
        if (sampler.interpolation == "STEP") return lastValue;
        const float wrappedSpan = (duration - lastTime) + firstTime;
        if (!(wrappedSpan > 0.0f)) return lastValue;
        const float a = std::clamp((t - lastTime) / wrappedSpan, 0.0f, 1.0f);
        return glm::mix(lastValue, firstValue, a);
    }
    const float t0 = sampler.inputs[i];
    const float t1 = sampler.inputs[i + 1u];
    const float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
    const glm::vec4 v0 = valueAt(i);
    const glm::vec4 v1 = valueAt(i + 1u);
    if (sampler.interpolation == "STEP") return v0;
    return glm::mix(v0, v1, std::clamp(a, 0.0f, 1.0f));
}

glm::vec4 sampleVec4(const engine::render::model_types::AnimationSampler& sampler,
                     float t,
                     float duration,
                     bool loopingClip) {
    if (!loopingClip) {
        return sampleVec4NoLoop(sampler, t);
    }

    const glm::vec4 baseValue = sampleVec4LoopBase(sampler, t, duration);
    const float blendWindowSec = sceneLoopClosureBlendWindowSec(duration);
    if (!(blendWindowSec > 0.0f) || !(blendWindowSec < duration)) {
        return baseValue;
    }

    if (t < blendWindowSec) {
        const float alpha = std::clamp(t / blendWindowSec, 0.0f, 1.0f);
        const float endTime = duration - blendWindowSec + t;
        const glm::vec4 endValue = sampleVec4LoopBase(sampler, endTime, duration);
        return glm::mix(endValue, baseValue, alpha);
    }
    if (t > duration - blendWindowSec) {
        const float alpha =
            std::clamp((t - (duration - blendWindowSec)) / blendWindowSec, 0.0f, 1.0f);
        const float startTime = t - (duration - blendWindowSec);
        const glm::vec4 startValue = sampleVec4LoopBase(sampler, startTime, duration);
        return glm::mix(baseValue, startValue, alpha);
    }

    return baseValue;
}

glm::quat sampleQuatNoLoop(const engine::render::model_types::AnimationSampler& sampler, float t) {
    if (sampler.inputs.empty() || sampler.outputs.empty()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const auto quatAt = [&](std::size_t index) {
        const glm::vec4 v = sampler.outputs[std::min(index, sampler.outputs.size() - 1u)];
        return normalizeQuatIfNeeded(glm::quat(v.w, v.x, v.y, v.z));
    };
    if (sampler.inputs.size() == 1u || sampler.outputs.size() == 1u) {
        return quatAt(0u);
    }

    const float firstTime = sampler.inputs.front();
    const float lastTime = sampler.inputs.back();
    const glm::quat lastValue = quatAt(sampler.inputs.size() - 1u);
    if (t <= firstTime) return quatAt(0u);
    if (t >= lastTime) return lastValue;

    const std::size_t i = findKeyframe(sampler.inputs, t);
    if (i >= sampler.inputs.size() - 1u) return lastValue;

    const float t0 = sampler.inputs[i];
    const float t1 = sampler.inputs[i + 1u];
    const float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
    const glm::quat q0 = quatAt(i);
    const glm::quat q1 = quatAt(i + 1u);
    if (sampler.interpolation == "STEP") return q0;
    return normalizeQuatIfNeeded(glm::slerp(q0, q1, std::clamp(a, 0.0f, 1.0f)));
}

glm::quat sampleQuatLoopBase(const engine::render::model_types::AnimationSampler& sampler,
                             float t,
                             float duration) {
    if (sampler.inputs.empty() || sampler.outputs.empty()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const auto quatAt = [&](std::size_t index) {
        const glm::vec4 v = sampler.outputs[std::min(index, sampler.outputs.size() - 1u)];
        return normalizeQuatIfNeeded(glm::quat(v.w, v.x, v.y, v.z));
    };
    if (sampler.inputs.size() == 1u || sampler.outputs.size() == 1u) {
        return quatAt(0u);
    }

    const float firstTime = sampler.inputs.front();
    const float lastTime = sampler.inputs.back();
    const glm::quat firstValue = quatAt(0u);
    const glm::quat lastValue = quatAt(sampler.inputs.size() - 1u);
    const bool outsideKeyRange = t < firstTime || t > lastTime;
    if (outsideKeyRange) {
        if (sampler.interpolation == "STEP") return lastValue;
        const float wrappedSpan = (duration - lastTime) + firstTime;
        if (!(wrappedSpan > 0.0f)) return lastValue;
        const float wrappedT =
            (t >= lastTime) ? (t - lastTime) : (t + duration - lastTime);
        const float a = std::clamp(wrappedT / wrappedSpan, 0.0f, 1.0f);
        return normalizeQuatIfNeeded(glm::slerp(lastValue, firstValue, a));
    }

    const std::size_t i = findKeyframe(sampler.inputs, t);
    if (i >= sampler.inputs.size() - 1u) {
        if (sampler.interpolation == "STEP") return lastValue;
        const float wrappedSpan = (duration - lastTime) + firstTime;
        if (!(wrappedSpan > 0.0f)) return lastValue;
        const float a = std::clamp((t - lastTime) / wrappedSpan, 0.0f, 1.0f);
        return normalizeQuatIfNeeded(glm::slerp(lastValue, firstValue, a));
    }

    const float t0 = sampler.inputs[i];
    const float t1 = sampler.inputs[i + 1u];
    const float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
    const glm::quat q0 = quatAt(i);
    const glm::quat q1 = quatAt(i + 1u);
    if (sampler.interpolation == "STEP") return q0;
    return normalizeQuatIfNeeded(glm::slerp(q0, q1, std::clamp(a, 0.0f, 1.0f)));
}

glm::quat sampleQuat(const engine::render::model_types::AnimationSampler& sampler,
                     float t,
                     float duration,
                     bool loopingClip) {
    if (!loopingClip) {
        return sampleQuatNoLoop(sampler, t);
    }

    const glm::quat baseValue = sampleQuatLoopBase(sampler, t, duration);
    const float blendWindowSec = sceneLoopClosureBlendWindowSec(duration);
    if (!(blendWindowSec > 0.0f) || !(blendWindowSec < duration)) {
        return baseValue;
    }

    if (t < blendWindowSec) {
        const float alpha = std::clamp(t / blendWindowSec, 0.0f, 1.0f);
        const float endTime = duration - blendWindowSec + t;
        const glm::quat endValue = sampleQuatLoopBase(sampler, endTime, duration);
        return normalizeQuatIfNeeded(glm::slerp(endValue, baseValue, alpha));
    }
    if (t > duration - blendWindowSec) {
        const float alpha =
            std::clamp((t - (duration - blendWindowSec)) / blendWindowSec, 0.0f, 1.0f);
        const float startTime = t - (duration - blendWindowSec);
        const glm::quat startValue = sampleQuatLoopBase(sampler, startTime, duration);
        return normalizeQuatIfNeeded(glm::slerp(baseValue, startValue, alpha));
    }

    return baseValue;
}

struct ScenePoseMeshCache {
    std::string assetCacheIdentitySnapshot;
    std::size_t nodeCountSnapshot = 0u;
    std::size_t animationCountSnapshot = 0u;
    std::vector<int> evalOrder;
    std::vector<glm::mat4> defaultLocalMatrices;
    std::vector<glm::mat4> defaultGlobalMatrices;
    std::vector<std::vector<std::uint8_t>> animatedNodeMaskByClip;
    std::vector<std::vector<int>> animatedNodesByClip;
    std::vector<std::vector<int>> affectedEvalOrderByClip;
};

const ScenePoseMeshCache& scenePoseMeshCacheFor(const render_model::MeshData& mesh) {
    static std::unordered_map<const render_model::MeshData*, ScenePoseMeshCache> cache;
    const auto found = cache.find(&mesh);
    if (found != cache.end() &&
        found->second.assetCacheIdentitySnapshot ==
            mesh.assetCacheIdentity &&
        found->second.nodeCountSnapshot ==
            mesh.nodesDefault.size() &&
        found->second.animationCountSnapshot ==
            mesh.animations.size()) {
        return found->second;
    }
    if (found != cache.end()) {
        cache.erase(found);
    }

    ScenePoseMeshCache built;
    const std::size_t nodeCount = mesh.nodesDefault.size();
    built.assetCacheIdentitySnapshot =
        mesh.assetCacheIdentity;
    built.nodeCountSnapshot = nodeCount;
    built.animationCountSnapshot =
        mesh.animations.size();
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

    built.defaultGlobalMatrices.resize(nodeCount, glm::mat4(1.0f));
    for (const int node : built.evalOrder) {
        if (node < 0 || static_cast<std::size_t>(node) >= nodeCount) continue;
        const std::size_t nodeIndex = static_cast<std::size_t>(node);
        const int parent =
            (nodeIndex < mesh.nodeParent.size()) ? mesh.nodeParent[nodeIndex] : -1;
        if (parent >= 0 && static_cast<std::size_t>(parent) < built.defaultGlobalMatrices.size()) {
            built.defaultGlobalMatrices[nodeIndex] =
                built.defaultGlobalMatrices[static_cast<std::size_t>(parent)] *
                built.defaultLocalMatrices[nodeIndex];
        } else {
            built.defaultGlobalMatrices[nodeIndex] = built.defaultLocalMatrices[nodeIndex];
        }
    }

    built.animatedNodeMaskByClip.resize(mesh.animations.size());
    built.animatedNodesByClip.resize(mesh.animations.size());
    built.affectedEvalOrderByClip.resize(mesh.animations.size());
    for (std::size_t clipIndex = 0; clipIndex < mesh.animations.size(); ++clipIndex) {
        auto& mask = built.animatedNodeMaskByClip[clipIndex];
        auto& animatedNodes = built.animatedNodesByClip[clipIndex];
        auto& affectedEvalOrder = built.affectedEvalOrderByClip[clipIndex];
        mask.assign(nodeCount, 0u);
        std::vector<std::uint8_t> affectedMask(nodeCount, 0u);
        stack.clear();
        for (const auto& channel : mesh.animations[clipIndex].channels) {
            if (channel.targetNode < 0) continue;
            const std::size_t nodeIndex = static_cast<std::size_t>(channel.targetNode);
            if (nodeIndex >= nodeCount) continue;
            if (mask[nodeIndex] == 0u) {
                mask[nodeIndex] = 1u;
                animatedNodes.push_back(channel.targetNode);
                affectedMask[nodeIndex] = 1u;
                stack.push_back(channel.targetNode);
            }
        }
        while (!stack.empty()) {
            const int node = stack.back();
            stack.pop_back();
            if (node < 0 || static_cast<std::size_t>(node) >= mesh.nodeChildren.size()) continue;
            for (const int child : mesh.nodeChildren[static_cast<std::size_t>(node)]) {
                if (child < 0 || static_cast<std::size_t>(child) >= nodeCount) continue;
                const std::size_t childIndex = static_cast<std::size_t>(child);
                if (affectedMask[childIndex] != 0u) continue;
                affectedMask[childIndex] = 1u;
                stack.push_back(child);
            }
        }
        affectedEvalOrder.reserve(animatedNodes.size());
        for (const int node : built.evalOrder) {
            if (node < 0 || static_cast<std::size_t>(node) >= nodeCount) continue;
            if (affectedMask[static_cast<std::size_t>(node)] != 0u) {
                affectedEvalOrder.push_back(node);
            }
        }
    }

    const auto inserted = cache.emplace(&mesh, std::move(built));
    return inserted.first->second;
}

struct RootMotionCarrierMaskCache {
    std::string assetCacheIdentitySnapshot;
    std::size_t nodeCountSnapshot = 0u;
    std::size_t skinCountSnapshot = 0u;
    const std::string* nodeNamesDataSnapshot = nullptr;
    std::vector<std::uint8_t> mask;
};

bool isSemanticOriginNodeName(std::string_view name) {
    const std::size_t separator = name.find_last_of("|/:\\");
    if (separator != std::string_view::npos) {
        name.remove_prefix(separator + 1u);
    }
    constexpr std::string_view expected = "origin";
    if (name.size() != expected.size()) return false;
    for (std::size_t i = 0u; i < name.size(); ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != expected[i]) return false;
    }
    return true;
}

const std::vector<std::uint8_t>& rootMotionCarrierMaskForMesh(const render_model::MeshData& mesh) {
    static std::unordered_map<
        const render_model::MeshData*,
        RootMotionCarrierMaskCache>
        cache;
    const auto found = cache.find(&mesh);
    if (found != cache.end() &&
        found->second.assetCacheIdentitySnapshot ==
            mesh.assetCacheIdentity &&
        found->second.nodeCountSnapshot ==
            mesh.nodesDefault.size() &&
        found->second.skinCountSnapshot ==
            mesh.skins.size() &&
        found->second.nodeNamesDataSnapshot == mesh.nodeNames.data()) {
        return found->second.mask;
    }
    if (found != cache.end()) {
        cache.erase(found);
    }

    RootMotionCarrierMaskCache built;
    built.assetCacheIdentitySnapshot =
        mesh.assetCacheIdentity;
    built.nodeCountSnapshot =
        mesh.nodesDefault.size();
    built.skinCountSnapshot = mesh.skins.size();
    built.nodeNamesDataSnapshot = mesh.nodeNames.data();
    built.mask.assign(mesh.nodesDefault.size(), 0u);
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
                static_cast<std::size_t>(jointNode) >= built.mask.size()) {
                continue;
            }
            const int parent = mesh.nodeParent[static_cast<std::size_t>(jointNode)];
            if (parent < 0 || jointSet.find(parent) == jointSet.end()) {
                built.mask[static_cast<std::size_t>(jointNode)] = 1u;
            }
        }
    }
    // Game Freak locomotion clips commonly place authored travel on a child
    // joint named "origin" beneath the skin root. It is still root motion, not
    // pose motion (hips/waist bobbing remains untouched).
    const std::size_t namedNodeCount =
        std::min(mesh.nodeNames.size(), built.mask.size());
    for (std::size_t nodeIndex = 0u; nodeIndex < namedNodeCount; ++nodeIndex) {
        if (isSemanticOriginNodeName(mesh.nodeNames[nodeIndex])) {
            built.mask[nodeIndex] = 1u;
        }
    }

    const auto inserted =
        cache.emplace(&mesh, std::move(built));
    return inserted.first->second.mask;
}

void buildGlobals(const render_model::MeshData& mesh, PoseEval& eval, int animIndex) {
    const auto& meshCache = scenePoseMeshCacheFor(mesh);
    if (meshCache.defaultGlobalMatrices.size() != eval.nodeGlobals.size()) return;
    std::copy(meshCache.defaultGlobalMatrices.begin(),
              meshCache.defaultGlobalMatrices.end(),
              eval.nodeGlobals.begin());

    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= meshCache.animatedNodeMaskByClip.size()) {
        return;
    }
    const auto& animatedNodeMask = meshCache.animatedNodeMaskByClip[static_cast<std::size_t>(animIndex)];
    const auto& affectedEvalOrder = meshCache.affectedEvalOrderByClip[static_cast<std::size_t>(animIndex)];
    for (const int node : affectedEvalOrder) {
        if (node < 0 || static_cast<std::size_t>(node) >= eval.nodeGlobals.size()) continue;
        const std::size_t nodeIndex = static_cast<std::size_t>(node);
        const glm::mat4 localM =
            (animatedNodeMask[nodeIndex] != 0u)
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

void applyClipPose(const render_model::MeshData& mesh,
                  PoseEval& eval,
                  int animIndex,
                  float animTimeSec,
                  RootMotionPolicy rootMotionPolicy,
                  bool loopingClip) {
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) return;
    const auto& clip = mesh.animations[static_cast<std::size_t>(animIndex)];
    const auto& meshCache = scenePoseMeshCacheFor(mesh);
    const float clipTime = wrapTime(animTimeSec, clip.durationSec);
    const std::vector<std::uint8_t>* rootMask = nullptr;
    if (rootMotionPolicy != RootMotionPolicy::PreserveAuthored) {
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
        if (channel.path == engine::render::model_types::ChannelPath::Translation) {
            if (sampledVec4ReadyBySampler[samplerIndex] == 0u) {
                sampledVec4BySampler[samplerIndex] =
                    sampleVec4(sampler, clipTime, clip.durationSec, loopingClip);
                sampledVec4ReadyBySampler[samplerIndex] = 1u;
            }
            const glm::vec4 tr = sampledVec4BySampler[samplerIndex];
            const bool rootMotionCarrier =
                rootMotionPolicy != RootMotionPolicy::PreserveAuthored &&
                rootMask &&
                (channel.targetNode >= 0) &&
                (static_cast<std::size_t>(channel.targetNode) < rootMask->size()) &&
                ((*rootMask)[static_cast<std::size_t>(channel.targetNode)] != 0u);
            if (rootMotionCarrier) {
                const auto& bind = mesh.nodesDefault[static_cast<std::size_t>(channel.targetNode)];
                if (bind.hasMatrix) {
                    local = bind;
                    local.matrix[3].x = bind.matrix[3].x;
                    local.matrix[3].y =
                        (rootMotionPolicy == RootMotionPolicy::InPlaceHorizontal)
                            ? tr.y
                            : bind.matrix[3].y;
                    local.matrix[3].z = bind.matrix[3].z;
                    local.matrix[3].w = 1.0f;
                    local.hasMatrix = true;
                } else {
                    local.t = glm::vec3(
                        bind.t.x,
                        (rootMotionPolicy == RootMotionPolicy::InPlaceHorizontal)
                            ? tr.y
                            : bind.t.y,
                        bind.t.z);
                    local.hasMatrix = false;
                }
            } else {
                local.t = glm::vec3(tr.x, tr.y, tr.z);
                local.hasMatrix = false;
            }
        } else if (channel.path == engine::render::model_types::ChannelPath::Scale) {
            if (sampledVec4ReadyBySampler[samplerIndex] == 0u) {
                sampledVec4BySampler[samplerIndex] =
                    sampleVec4(sampler, clipTime, clip.durationSec, loopingClip);
                sampledVec4ReadyBySampler[samplerIndex] = 1u;
            }
            const glm::vec4 sc = sampledVec4BySampler[samplerIndex];
            local.s = glm::vec3(sc.x, sc.y, sc.z);
            local.hasMatrix = false;
        } else if (channel.path == engine::render::model_types::ChannelPath::Rotation) {
            if (sampledQuatReadyBySampler[samplerIndex] == 0u) {
                sampledQuatBySampler[samplerIndex] =
                    sampleQuat(sampler, clipTime, clip.durationSec, loopingClip);
                sampledQuatReadyBySampler[samplerIndex] = 1u;
            }
            local.r = sampledQuatBySampler[samplerIndex];
            local.hasMatrix = false;
        }
    }
    eval.hasClipPose = true;
}

int resolveSceneAnimIndex(const render_model::MeshData& mesh, const PokemonInstance& unit) {
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

bool isSceneLoopingClipForUnit(const PokemonInstance& unit, int animIndex) {
    if (animIndex < 0) return false;
    if (unit.attackTimerSec > 0.0f) return false;
    if (animIndex == unit.animFaintIndex) return false;
    if (animIndex == unit.animTakeoffIndex ||
        animIndex == unit.animLandIndex ||
        animIndex == unit.animLandAIndex ||
        animIndex == unit.animLandCIndex) {
        return false;
    }
    return animIndex == unit.animIdleIndex ||
           animIndex == unit.animMoveIndex ||
           animIndex == unit.animGroundIdleIndex ||
           animIndex == unit.animAirIdleIndex ||
           animIndex == unit.animLandBIndex;
}

void resetPoseEvalForMesh(const render_model::MeshData& mesh, PoseEval& eval) {
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

bool shouldTreatSceneClipAsLooping(const PokemonInstance& unit, int animIndex) {
    return isSceneLoopingClipForUnit(unit, animIndex);
}

void evaluateScenePose(const render_model::MeshData& mesh,
                       const PokemonInstance& unit,
                       PoseEval& outPose) {
    const int animIndex = resolveSceneAnimIndex(mesh, unit);
    evaluateScenePoseForResolvedClipTime(
        mesh,
        animIndex,
        unit.animTimeSec,
        RootMotionPolicy::InPlaceHorizontal,
        isSceneLoopingClipForUnit(unit, animIndex),
        outPose);
}

PoseEval evaluateScenePose(const render_model::MeshData& mesh, const PokemonInstance& unit) {
    PoseEval eval;
    evaluateScenePose(mesh, unit, eval);
    return eval;
}

void evaluateScenePoseForResolvedClipTime(const render_model::MeshData& mesh,
                                          int animIndex,
                                          float animTimeSec,
                                          RootMotionPolicy rootMotionPolicy,
                                          PoseEval& outPose) {
    evaluateScenePoseForResolvedClipTime(
        mesh,
        animIndex,
        animTimeSec,
        rootMotionPolicy,
        false,
        outPose);
}

void evaluateScenePoseForResolvedClipTime(const render_model::MeshData& mesh,
                                          int animIndex,
                                          float animTimeSec,
                                          RootMotionPolicy rootMotionPolicy,
                                          bool loopingClip,
                                          PoseEval& outPose) {
    if (mesh.nodesDefault.empty()) {
        outPose = PoseEval{};
        return;
    }
    resetPoseEvalForMesh(mesh, outPose);

    if (animIndex >= 0) {
        applyClipPose(
            mesh,
            outPose,
            animIndex,
            animTimeSec,
            rootMotionPolicy,
            loopingClip);
    }

    buildGlobals(mesh, outPose, animIndex);
}

PoseEval evaluateScenePoseForResolvedClipTime(const render_model::MeshData& mesh,
                                              int animIndex,
                                              float animTimeSec,
                                              RootMotionPolicy rootMotionPolicy) {
    return evaluateScenePoseForResolvedClipTime(
        mesh,
        animIndex,
        animTimeSec,
        rootMotionPolicy,
        false);
}

PoseEval evaluateScenePoseForResolvedClipTime(const render_model::MeshData& mesh,
                                              int animIndex,
                                              float animTimeSec,
                                              RootMotionPolicy rootMotionPolicy,
                                              bool loopingClip) {
    PoseEval eval;
    evaluateScenePoseForResolvedClipTime(
        mesh,
        animIndex,
        animTimeSec,
        rootMotionPolicy,
        loopingClip,
        eval);
    return eval;
}

void evaluateScenePoseForClipTime(const render_model::MeshData& mesh,
                                  int animIndex,
                                  float animTimeSec,
                                  PoseEval& outPose) {
    evaluateScenePoseForResolvedClipTime(
        mesh,
        animIndex,
        animTimeSec,
        RootMotionPolicy::PreserveAuthored,
        outPose);
}

PoseEval evaluateScenePoseForClipTime(const render_model::MeshData& mesh,
                                      int animIndex,
                                      float animTimeSec) {
    return evaluateScenePoseForResolvedClipTime(
        mesh,
        animIndex,
        animTimeSec,
        RootMotionPolicy::PreserveAuthored);
}

} // namespace game::runtime::shared_backend_pose
