#include "game/runtime/shared/backend/SharedBackendPoseEval.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace game::runtime::shared_backend_pose {
namespace {

glm::mat4 trsToMat4(const pac_model_types::NodeTRS& n) {
    if (n.hasMatrix) return n.matrix;
    const glm::mat4 t = glm::translate(glm::mat4(1.0f), n.t);
    const glm::mat4 r = glm::mat4_cast(glm::normalize(n.r));
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

glm::quat sampleQuat(const pac_model_types::AnimationSampler& sampler, float t) {
    const glm::vec4 v = sampleVec4(sampler, t);
    return glm::normalize(glm::quat(v.w, v.x, v.y, v.z));
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

void buildGlobals(const backend_model::MeshData& mesh, PoseEval& eval) {
    const auto dfs = [&](const auto& self, int node, const glm::mat4& parentM) -> void {
        if (node < 0 || static_cast<std::size_t>(node) >= eval.nodeLocals.size()) return;
        const glm::mat4 global = parentM * trsToMat4(eval.nodeLocals[static_cast<std::size_t>(node)]);
        eval.nodeGlobals[static_cast<std::size_t>(node)] = global;
        if (static_cast<std::size_t>(node) >= mesh.nodeChildren.size()) return;
        for (int child : mesh.nodeChildren[static_cast<std::size_t>(node)]) {
            self(self, child, global);
        }
    };

    if (!mesh.sceneRoots.empty()) {
        for (int root : mesh.sceneRoots) {
            dfs(dfs, root, glm::mat4(1.0f));
        }
    } else if (!eval.nodeLocals.empty()) {
        bool drewAny = false;
        for (std::size_t i = 0; i < mesh.nodeParent.size(); ++i) {
            if (mesh.nodeParent[i] >= 0) continue;
            dfs(dfs, static_cast<int>(i), glm::mat4(1.0f));
            drewAny = true;
        }
        if (!drewAny) dfs(dfs, 0, glm::mat4(1.0f));
    }
}

void applyClipPose(const backend_model::MeshData& mesh,
                  PoseEval& eval,
                  int animIndex,
                  float animTimeSec,
                  bool preserveRootMotionCarrierXZ) {
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) return;
    const auto& clip = mesh.animations[static_cast<std::size_t>(animIndex)];
    const float clipTime = wrapTime(animTimeSec, clip.durationSec);
    const std::vector<std::uint8_t>* rootMask = nullptr;
    if (preserveRootMotionCarrierXZ) {
        rootMask = &rootMotionCarrierMaskForMesh(mesh);
    }

    for (const auto& channel : clip.channels) {
        if (channel.targetNode < 0 || static_cast<std::size_t>(channel.targetNode) >= eval.nodeLocals.size()) {
            continue;
        }
        if (channel.samplerIndex < 0 || static_cast<std::size_t>(channel.samplerIndex) >= clip.samplers.size()) {
            continue;
        }
        auto& local = eval.nodeLocals[static_cast<std::size_t>(channel.targetNode)];
        const auto& sampler = clip.samplers[static_cast<std::size_t>(channel.samplerIndex)];
        if (channel.path == pac_model_types::ChannelPath::Translation) {
            const glm::vec4 tr = sampleVec4(sampler, clipTime);
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
            const glm::vec4 sc = sampleVec4(sampler, clipTime);
            local.s = glm::vec3(sc.x, sc.y, sc.z);
            local.hasMatrix = false;
        } else if (channel.path == pac_model_types::ChannelPath::Rotation) {
            local.r = sampleQuat(sampler, clipTime);
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

} // namespace

PoseEval evaluateScenePose(const backend_model::MeshData& mesh, const PokemonInstance& unit) {
    PoseEval eval;
    if (mesh.nodesDefault.empty()) return eval;
    eval.hasScenePose = true;
    eval.nodeLocals = mesh.nodesDefault;
    eval.nodeGlobals.assign(mesh.nodesDefault.size(), glm::mat4(1.0f));

    const int animIndex = resolveSceneAnimIndex(mesh, unit);
    if (animIndex >= 0) {
        applyClipPose(mesh, eval, animIndex, unit.animTimeSec, true);
    }

    buildGlobals(mesh, eval);
    return eval;
}

PoseEval evaluateScenePoseForClipTime(const backend_model::MeshData& mesh,
                                      int animIndex,
                                      float animTimeSec) {
    PoseEval eval;
    if (mesh.nodesDefault.empty()) return eval;
    eval.hasScenePose = true;
    eval.nodeLocals = mesh.nodesDefault;
    eval.nodeGlobals.assign(mesh.nodesDefault.size(), glm::mat4(1.0f));

    if (animIndex >= 0) {
        applyClipPose(mesh, eval, animIndex, animTimeSec, false);
    }

    buildGlobals(mesh, eval);
    return eval;
}

} // namespace game::runtime::shared_backend_pose
