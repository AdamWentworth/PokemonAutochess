// src/engine/render/ModelAnimation.cpp
//
// Animation + skinning draw path split out of Model.cpp.
//
// This is now a normal .cpp because ModelAnimationTypes.h moved the nested types
// (NodeTRS / AnimationSampler / AnimationClip / etc.) out of Model's private section.

#include "Model.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

void Model::uploadSkinUniforms(const glm::mat4& meshGlobal,
                               int skinIndex,
                               const std::vector<glm::mat4>& nodeGlobals) const
{
    if (skinIndex < 0 || skinIndex >= (int)skins.size()) {
        glUniform1i(locUseSkin, 0);
        return;
    }

    const auto& skin = skins[skinIndex];
    if (skin.joints.empty() || skin.inverseBind.size() != skin.joints.size()) {
        glUniform1i(locUseSkin, 0);
        return;
    }

    constexpr int MAX_JOINTS = 128;
    if ((int)skin.joints.size() > MAX_JOINTS) {
        std::cerr << "[Model] WARNING: skin joints exceed MAX_JOINTS=128; skinning disabled.\n";
        glUniform1i(locUseSkin, 0);
        return;
    }

    glm::mat4 invMesh = glm::inverse(meshGlobal);

    std::vector<glm::mat4> mats;
    mats.resize(skin.joints.size(), glm::mat4(1.0f));

    for (size_t i = 0; i < skin.joints.size(); ++i) {
        int jNode = skin.joints[i];
        if (jNode < 0 || jNode >= (int)nodeGlobals.size()) {
            mats[i] = glm::mat4(1.0f);
            continue;
        }
        mats[i] = invMesh * nodeGlobals[jNode] * skin.inverseBind[i];
    }

    glUniform1i(locUseSkin, 1);
    glUniformMatrix4fv(locJoints0, (GLsizei)mats.size(), GL_FALSE, glm::value_ptr(mats[0]));
}

static float wrapTime(float t, float duration)
{
    if (duration <= 0.0f) return 0.0f;
    float x = std::fmod(t, duration);
    if (x < 0.0f) x += duration;
    return x;
}

static size_t findKeyframe(const std::vector<float>& inputs, float t)
{
    if (inputs.empty()) return 0;
    if (t <= inputs.front()) return 0;
    if (t >= inputs.back()) return inputs.size() - 1;

    auto it = std::upper_bound(inputs.begin(), inputs.end(), t);
    size_t idx = (size_t)std::distance(inputs.begin(), it);
    if (idx == 0) return 0;
    return idx - 1;
}

static float safeLerpT(float t, float t0, float t1)
{
    float dt = (t1 - t0);
    if (dt <= 1e-6f) return 0.0f;
    return (t - t0) / dt;
}

static glm::vec3 lerpVec3(const glm::vec3& a, const glm::vec3& b, float t)
{
    return a + (b - a) * t;
}

static glm::quat slerpQuat(const glm::quat& a, const glm::quat& b, float t)
{
    return glm::normalize(glm::slerp(a, b, t));
}

void Model::buildPoseMatrices(float timeSec,
                              int animIndex,
                              std::vector<NodeTRS>& outLocal,
                              std::vector<glm::mat4>& outGlobal) const
{
    // start from default pose
    outLocal = nodesDefault;

    // apply animation if valid
    if (animIndex >= 0 && animIndex < (int)animations.size()) {
        const auto& clip = animations[(size_t)animIndex];
        float t = wrapTime(timeSec, clip.durationSec);

        for (const auto& ch : clip.channels) {
            if (ch.targetNode < 0 || ch.targetNode >= (int)outLocal.size()) continue;
            if (ch.samplerIndex < 0 || ch.samplerIndex >= (int)clip.samplers.size()) continue;

            const auto& s = clip.samplers[(size_t)ch.samplerIndex];
            if (s.inputs.empty() || s.outputs.empty()) continue;

            size_t k0 = findKeyframe(s.inputs, t);
            size_t k1 = std::min(k0 + 1, s.inputs.size() - 1);

            float t0 = s.inputs[k0];
            float t1 = s.inputs[k1];
            float u = safeLerpT(t, t0, t1);

            glm::vec4 v0 = s.outputs[std::min(k0, s.outputs.size() - 1)];
            glm::vec4 v1 = s.outputs[std::min(k1, s.outputs.size() - 1)];

            if (ch.path == ChannelPath::Translation) {
                glm::vec3 a(v0.x, v0.y, v0.z);
                glm::vec3 b(v1.x, v1.y, v1.z);
                outLocal[ch.targetNode].t = lerpVec3(a, b, u);
            } else if (ch.path == ChannelPath::Scale) {
                glm::vec3 a(v0.x, v0.y, v0.z);
                glm::vec3 b(v1.x, v1.y, v1.z);
                outLocal[ch.targetNode].s = lerpVec3(a, b, u);
            } else if (ch.path == ChannelPath::Rotation) {
                glm::quat qa(v0.w, v0.x, v0.y, v0.z);
                glm::quat qb(v1.w, v1.x, v1.y, v1.z);
                outLocal[ch.targetNode].r = slerpQuat(qa, qb, u);
            }
        }
    }

    // globals via DFS
    outGlobal.assign(outLocal.size(), glm::mat4(1.0f));

    std::function<void(int, const glm::mat4&)> dfs =
        [&](int node, const glm::mat4& parent) {
            if (node < 0 || node >= (int)outLocal.size()) return;

            glm::mat4 local = trsToMat4(outLocal[node]);
            glm::mat4 global = parent * local;
            outGlobal[node] = global;

            if (node < (int)nodeChildren.size()) {
                for (int c : nodeChildren[node]) {
                    dfs(c, global);
                }
            }
        };

    if (sceneRoots.empty()) {
        dfs(0, glm::mat4(1.0f));
    } else {
        for (int r : sceneRoots) dfs(r, glm::mat4(1.0f));
    }
}

void Model::drawAnimated(const Camera3D& camera,
                         const glm::mat4& instanceTransform,
                         float animTimeSec,
                         int animIndex) const
{
    if (!modelShader || VAO == 0) return;

    if (animIndex == 1 && (int)animations.size() <= 1) {
        if (warnedMissingAnimIndex.insert(animIndex).second) {
            std::cout << "[Model] NOTE: requested animation index 1, but model has "
                      << animations.size() << " animation(s). Drawing static.\n";
        }
    }

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    buildPoseMatrices(animTimeSec, animIndex, locals, globals);

    modelShader->use();
    glBindVertexArray(VAO);

    // --- Save GL state (critical: UI relies on these) ---
    
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevBlend     = glIsEnabled(GL_BLEND);
    GLboolean prevCull      = glIsEnabled(GL_CULL_FACE);

    GLint prevDepthMask = GL_TRUE;
    glGetIntegerv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    GLint prevBlendSrcRGB = GL_ONE, prevBlendDstRGB = GL_ZERO;
    GLint prevBlendSrcA   = GL_ONE, prevBlendDstA   = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB,   &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB,   &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstA);

    GLint prevEqRGB = GL_FUNC_ADD, prevEqA = GL_FUNC_ADD;
    glGetIntegerv(GL_BLEND_EQUATION_RGB,   &prevEqRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevEqA);

    // bind sampler units once (safe even if uniforms are optimized out)
    if (locBaseColorTex >= 0) glUniform1i(locBaseColorTex, 0);
    if (locEmissiveTex  >= 0) glUniform1i(locEmissiveTex,  1);

    // tone mapping (set every draw in case some other renderer touched the same program)
    if (locTonemapMode >= 0) glUniform1i(locTonemapMode, 1); // 1=ACES
    if (locExposure    >= 0) glUniform1f(locExposure, 1.0f);

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

        // BLEND must enable blending AND disable depth writes.
        if (sm.alphaMode == 2) { // BLEND
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        } else {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }
    };

    // We assume you want depth test on for models.
    glEnable(GL_DEPTH_TEST);

    auto drawMeshAtNode = [&](int node, const glm::mat4& meshGlobal) {
        int meshIndex = -1;
        int skinIndex = -1;

        if (node >= 0 && node < (int)nodeMesh.size()) meshIndex = nodeMesh[node];
        if (node >= 0 && node < (int)nodeSkin.size()) skinIndex = nodeSkin[node];

        glm::mat4 vp  = camera.getProjectionMatrix() * camera.getViewMatrix();
        glm::mat4 mvp = vp * instanceTransform * meshGlobal;
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(mvp));

        // upload skinning
        uploadSkinUniforms(meshGlobal, skinIndex, globals);

        // Draw submeshes that match this meshIndex
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto& sm : submeshes) {
                if (sm.meshIndex != meshIndex) continue;

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

    // determine if nodeMesh exists
    bool hasNodeMesh = !nodeMesh.empty();

    auto dfsDraw = [&](auto&& self, int node, const glm::mat4& parent) -> void {
        if (node < 0 || node >= (int)locals.size()) return;

        glm::mat4 localM  = trsToMat4(locals[node]);
        glm::mat4 globalM = parent * localM;

        drawMeshAtNode(node, globalM);

        if (node < (int)nodeChildren.size()) {
            for (int c : nodeChildren[node]) self(self, c, globalM);
        }
    };

    if (sceneRoots.empty()) {
        dfsDraw(dfsDraw, 0, glm::mat4(1.0f));
    } else {
        for (int r : sceneRoots) dfsDraw(dfsDraw, r, glm::mat4(1.0f));
    }

    // fallback: if nodeMesh is empty, draw everything once with instanceTransform
    if (!hasNodeMesh) {
        glm::mat4 vp  = camera.getProjectionMatrix() * camera.getViewMatrix();
        glm::mat4 mvp = vp * instanceTransform;
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform1i(locUseSkin, 0);

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

    // Restore saved GL state (covers edge cases too)
    if (prevDepthTest) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);

    if (prevBlend) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);

    if (prevCull) glEnable(GL_CULL_FACE);
    else glDisable(GL_CULL_FACE);

    glDepthMask((GLboolean)prevDepthMask);

    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcA, prevBlendDstA);
    glBlendEquationSeparate(prevEqRGB, prevEqA);

    glBindVertexArray(0);
}
