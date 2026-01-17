// src/engine/render/ModelAnimation.cpp
//
// Animation + skinning draw path split out of Model.cpp.
//

#include "Model.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

static glm::mat4 localTrsToMat4(const pac_model_types::NodeTRS& n)
{
    if (n.hasMatrix) return n.matrix;

    glm::mat4 t = glm::translate(glm::mat4(1.0f), n.t);
    glm::mat4 r = glm::toMat4(n.r);
    glm::mat4 s = glm::scale(glm::mat4(1.0f), n.s);
    return t * r * s;
}

static float wrapTime(float t, float duration)
{
    if (duration <= 0.0f) return t;
    float w = std::fmod(t, duration);
    if (w < 0.0f) w += duration;
    return w;
}

// Interpolate TRS for one node at time t, returning a local matrix.
// This uses your AnimationSampler encoding: inputs = key times, outputs = vec4s. :contentReference[oaicite:1]{index=1}
static glm::mat4 interpolateLocalTRS(const pac_model_types::NodeTRS& base,
                                    const pac_model_types::AnimationClip& clip,
                                    float timeSec,
                                    int nodeIndex)
{
    using ChannelPath = pac_model_types::ChannelPath;
    using AnimationSampler = pac_model_types::AnimationSampler;

    pac_model_types::NodeTRS out = base;

    auto sampleVec4 = [&](const AnimationSampler& s, float t) -> glm::vec4 {
        if (s.inputs.empty() || s.outputs.empty()) return glm::vec4(0.0f);

        // Find segment
        size_t i = 0;
        while (i + 1 < s.inputs.size() && s.inputs[i + 1] < t) ++i;
        size_t i2 = std::min(i + 1, s.inputs.size() - 1);

        float t0 = s.inputs[i];
        float t1 = s.inputs[i2];
        float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
        a = std::clamp(a, 0.0f, 1.0f);

        glm::vec4 v0 = s.outputs[std::min(i,  s.outputs.size() - 1)];
        glm::vec4 v1 = s.outputs[std::min(i2, s.outputs.size() - 1)];

        if (s.interpolation == "STEP") return v0;
        return glm::mix(v0, v1, a);
    };

    auto sampleQuat = [&](const AnimationSampler& s, float t) -> glm::quat {
        glm::vec4 v = sampleVec4(s, t);
        glm::quat q(v.w, v.x, v.y, v.z);
        return glm::normalize(q);
    };

    if (!clip.channels.empty()) {
        float t = wrapTime(timeSec, clip.durationSec);

        for (const auto& ch : clip.channels) {
            if (ch.targetNode != nodeIndex) continue;
            if (ch.samplerIndex < 0 || ch.samplerIndex >= (int)clip.samplers.size()) continue;

            const auto& s = clip.samplers[(size_t)ch.samplerIndex];

            if (ch.path == ChannelPath::Translation) {
                glm::vec4 v = sampleVec4(s, t);
                out.t = glm::vec3(v.x, v.y, v.z);
                out.hasMatrix = false;
            } else if (ch.path == ChannelPath::Scale) {
                glm::vec4 v = sampleVec4(s, t);
                out.s = glm::vec3(v.x, v.y, v.z);
                out.hasMatrix = false;
            } else if (ch.path == ChannelPath::Rotation) {
                out.r = sampleQuat(s, t);
                out.hasMatrix = false;
            }
        }
    }

    return localTrsToMat4(out);
}

void Model::buildPoseMatrices(float timeSec,
                              int animIndex,
                              std::vector<NodeTRS>& outLocal,
                              std::vector<glm::mat4>& outGlobal) const
{
    outLocal = nodesDefault;
    outGlobal.assign(nodesDefault.size(), glm::mat4(1.0f));

    const pac_model_types::AnimationClip* clip = nullptr;
    if (animIndex >= 0 && animIndex < (int)animations.size()) {
        clip = &animations[(size_t)animIndex];
    }

    if (clip) {
        for (size_t i = 0; i < outLocal.size(); ++i) {
            outLocal[i].hasMatrix = true;
            outLocal[i].matrix = interpolateLocalTRS(outLocal[i], *clip, timeSec, (int)i);
        }
    }

    auto dfs = [&](auto&& self, int node, const glm::mat4& parent) -> void {
        if (node < 0 || node >= (int)outLocal.size()) return;

        glm::mat4 localM  = localTrsToMat4(outLocal[(size_t)node]);
        glm::mat4 globalM = parent * localM;
        outGlobal[(size_t)node] = globalM;

        if ((size_t)node < nodeChildren.size()) {
            for (int c : nodeChildren[(size_t)node]) {
                self(self, c, globalM);
            }
        }
    };

    if (!sceneRoots.empty()) {
        for (int r : sceneRoots) dfs(dfs, r, glm::mat4(1.0f));
    } else if (!outLocal.empty()) {
        dfs(dfs, 0, glm::mat4(1.0f));
    }
}

void Model::uploadSkinUniforms(const glm::mat4& meshGlobal,
                               int skinIndex,
                               const std::vector<glm::mat4>& nodeGlobals) const
{
    if (locUseSkin < 0 || locJoints0 < 0) return;

    if (skinIndex < 0 || skinIndex >= (int)skins.size()) {
        glUniform1i(locUseSkin, 0);
        return;
    }

    const auto& skin = skins[(size_t)skinIndex];
    const int jointCount = (int)skin.joints.size();

    if (jointCount <= 0 || (int)skin.inverseBind.size() < jointCount) {
        glUniform1i(locUseSkin, 0);
        return;
    }

    glUniform1i(locUseSkin, 1);

    std::vector<glm::mat4> jointMats((size_t)jointCount);
    glm::mat4 invMeshGlobal = glm::inverse(meshGlobal);

    for (int j = 0; j < jointCount; ++j) {
        int nodeIndex = skin.joints[(size_t)j];
        glm::mat4 jointGlobal = glm::mat4(1.0f);
        if (nodeIndex >= 0 && nodeIndex < (int)nodeGlobals.size()) {
            jointGlobal = nodeGlobals[(size_t)nodeIndex];
        }

        glm::mat4 ibm = skin.inverseBind[(size_t)j];
        jointMats[(size_t)j] = invMeshGlobal * jointGlobal * ibm;
    }

    glUniformMatrix4fv(locJoints0, jointCount, GL_FALSE, glm::value_ptr(jointMats[0]));
}

void Model::drawAnimated(const Camera3D& camera,
                         const glm::mat4& instanceTransform,
                         float animTimeSec,
                         int animIndex) const
{
    if (!modelShader || VAO == 0) return;

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    buildPoseMatrices(animTimeSec, animIndex, locals, globals);

    modelShader->use();
    glBindVertexArray(VAO);

    // bind sampler units once (safe even if uniforms are optimized out)
    if (locBaseColorTex >= 0) glUniform1i(locBaseColorTex, 0);
    if (locEmissiveTex  >= 0) glUniform1i(locEmissiveTex,  1);

    bool hasNodeMesh = false;

    auto setMaterial = [&](const Submesh& sm) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sm.baseColorTexID);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sm.emissiveTexID);

        if (locEmissiveFactor >= 0) glUniform3fv(locEmissiveFactor, 1, glm::value_ptr(sm.emissiveFactor));
        if (locAlphaMode      >= 0) glUniform1i(locAlphaMode, sm.alphaMode);
        if (locAlphaCutoff    >= 0) glUniform1f(locAlphaCutoff, sm.alphaCutoff);

        if (sm.doubleSided) glDisable(GL_CULL_FACE);
        else glEnable(GL_CULL_FACE);

        // Critical for the fire: BLEND must enable blending AND disable depth writes. :contentReference[oaicite:2]{index=2}
        if (sm.alphaMode == 2) { // BLEND
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        } else {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }
    };

    auto drawMeshAtNode = [&](int nodeIdx, const glm::mat4& nodeGlobal) {
        if (nodeIdx < 0 || nodeIdx >= (int)nodeMesh.size()) return;
        int meshIdx = nodeMesh[(size_t)nodeIdx];
        if (meshIdx < 0) return;

        hasNodeMesh = true;

        glm::mat4 vp  = camera.getProjectionMatrix() * camera.getViewMatrix();
        glm::mat4 mvp = vp * instanceTransform * nodeGlobal;
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(mvp));

        int skinIdx = (nodeIdx < (int)nodeSkin.size()) ? nodeSkin[(size_t)nodeIdx] : -1;
        uploadSkinUniforms(nodeGlobal, skinIdx, globals);

        // Two-pass inside the mesh: opaque/mask first, then blend.
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto& sm : submeshes) {
                if (sm.meshIndex != meshIdx) continue;

                const bool isBlend = (sm.alphaMode == 2);
                if ((pass == 0 && isBlend) || (pass == 1 && !isBlend)) continue;

                setMaterial(sm);

                glDrawElements(GL_TRIANGLES,
                               (GLsizei)sm.indexCount,
                               GL_UNSIGNED_INT,
                               (void*)(sm.indexOffset * sizeof(uint32_t)));
            }
        }
    };

    std::function<void(int, const glm::mat4&)> dfsDraw =
        [&](int node, const glm::mat4& parent) {
            if (node < 0 || node >= (int)locals.size()) return;

            glm::mat4 localM  = localTrsToMat4(locals[(size_t)node]);
            glm::mat4 globalM = parent * localM;

            drawMeshAtNode(node, globalM);

            if ((size_t)node < nodeChildren.size()) {
                for (int c : nodeChildren[(size_t)node]) {
                    if (c >= 0 && c < (int)locals.size()) dfsDraw(c, globalM);
                }
            }
        };

    if (hasNodeMesh) {
        for (int root : sceneRoots) {
            if (root >= 0 && root < (int)locals.size()) dfsDraw(root, glm::mat4(1.0f));
        }
    } else if (!sceneRoots.empty()) {
        for (int r : sceneRoots) dfsDraw(r, glm::mat4(1.0f));
    } else if (!locals.empty()) {
        dfsDraw(0, glm::mat4(1.0f));
    }

    // fallback: if nodeMesh is empty, draw everything once with instanceTransform
    if (!hasNodeMesh) {
        glm::mat4 vp  = camera.getProjectionMatrix() * camera.getViewMatrix();
        glm::mat4 mvp = vp * instanceTransform;
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(mvp));
        if (locUseSkin >= 0) glUniform1i(locUseSkin, 0);

        // opaque/mask then blend
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto& sm : submeshes) {
                const bool isBlend = (sm.alphaMode == 2);
                if ((pass == 0 && isBlend) || (pass == 1 && !isBlend)) continue;

                setMaterial(sm);

                glDrawElements(GL_TRIANGLES,
                               (GLsizei)sm.indexCount,
                               GL_UNSIGNED_INT,
                               (void*)(sm.indexOffset * sizeof(uint32_t)));
            }
        }
    }

    // restore common state (avoid leaking blend/depth-write into other renderers)
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}
