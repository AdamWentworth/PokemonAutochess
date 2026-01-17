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

static size_t findKeyframe(const std::vector<float>& times, float t)
{
    if (times.empty()) return 0;
    if (t <= times.front()) return 0;
    if (t >= times.back()) return times.size() - 1;

    auto it = std::upper_bound(times.begin(), times.end(), t);
    size_t idx = (it == times.begin()) ? 0 : (size_t)((it - times.begin()) - 1);
    return idx;
}

void Model::buildPoseMatrices(float timeSec,
                              int animIndex,
                              std::vector<NodeTRS>& outLocal,
                              std::vector<glm::mat4>& outGlobal) const
{
    outLocal = nodesDefault;
    outGlobal.assign(nodesDefault.size(), glm::mat4(1.0f));

    if (nodesDefault.empty()) return;

    auto sampleVec4 = [&](const AnimationSampler& s, float t)->glm::vec4 {
        if (s.inputs.empty() || s.outputs.empty()) return glm::vec4(0.0f);

        size_t i = findKeyframe(s.inputs, t);
        if (i >= s.inputs.size() - 1) {
            return s.outputs[std::min(i, s.outputs.size() - 1)];
        }

        float t0 = s.inputs[i];
        float t1 = s.inputs[i + 1];
        float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;

        glm::vec4 v0 = s.outputs[std::min(i, s.outputs.size() - 1)];
        glm::vec4 v1 = s.outputs[std::min(i + 1, s.outputs.size() - 1)];

        if (s.interpolation == "STEP") return v0;
        return glm::mix(v0, v1, a);
    };

    auto sampleQuat = [&](const AnimationSampler& s, float t)->glm::quat {
        glm::vec4 v = sampleVec4(s, t);
        glm::quat q(v.w, v.x, v.y, v.z);
        q = glm::normalize(q);
        return q;
    };

    if (animIndex >= 0 && animIndex < (int)animations.size()) {
        const auto& clip = animations[animIndex];
        float t = wrapTime(timeSec, clip.durationSec);

        for (const auto& ch : clip.channels) {
            if (ch.targetNode < 0 || ch.targetNode >= (int)outLocal.size()) continue;
            if (ch.samplerIndex < 0 || ch.samplerIndex >= (int)clip.samplers.size()) continue;
            const auto& s = clip.samplers[ch.samplerIndex];

            if (ch.path == ChannelPath::Translation) {
                glm::vec4 v = sampleVec4(s, t);
                outLocal[ch.targetNode].t = glm::vec3(v.x, v.y, v.z);
                outLocal[ch.targetNode].hasMatrix = false;
            } else if (ch.path == ChannelPath::Scale) {
                glm::vec4 v = sampleVec4(s, t);
                outLocal[ch.targetNode].s = glm::vec3(v.x, v.y, v.z);
                outLocal[ch.targetNode].hasMatrix = false;
            } else if (ch.path == ChannelPath::Rotation) {
                outLocal[ch.targetNode].r = sampleQuat(s, t);
                outLocal[ch.targetNode].hasMatrix = false;
            }
        }
    }

    std::function<void(int, const glm::mat4&)> dfs = [&](int node, const glm::mat4& parent) {
        if (node < 0 || node >= (int)outLocal.size()) return;
        glm::mat4 localM = trsToMat4(outLocal[node]);
        glm::mat4 global = parent * localM;
        outGlobal[node] = global;

        if (node < (int)nodeChildren.size()) {
            for (int c : nodeChildren[node]) dfs(c, global);
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
    const GLboolean prevCullEnabled  = glIsEnabled(GL_CULL_FACE);
    const GLboolean prevBlendEnabled = glIsEnabled(GL_BLEND);

    GLboolean prevDepthWrite = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthWrite);

    GLint prevActiveTex = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);

    GLint prevSrcRGB = GL_ONE, prevDstRGB = GL_ZERO, prevSrcA = GL_ONE, prevDstA = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB,   &prevSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB,   &prevDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevDstA);

    GLint prevEqRGB = GL_FUNC_ADD, prevEqA = GL_FUNC_ADD;
    glGetIntegerv(GL_BLEND_EQUATION_RGB,   &prevEqRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevEqA);

    // bind sampler units once (safe even if uniforms are optimized out)
    if (locBaseColorTex >= 0) glUniform1i(locBaseColorTex, 0);
    if (locEmissiveTex  >= 0) glUniform1i(locEmissiveTex,  1);

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

    bool hasNodeMesh = false;

    auto drawMeshAtNode = [&](int nodeIdx, const glm::mat4& nodeGlobal) {
        if (nodeIdx < 0 || nodeIdx >= (int)nodeMesh.size()) return;
        int meshIdx = nodeMesh[nodeIdx];
        if (meshIdx < 0) return;

        hasNodeMesh = true;

        glm::mat4 vp  = camera.getProjectionMatrix() * camera.getViewMatrix();
        glm::mat4 mvp = vp * instanceTransform * nodeGlobal;
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(mvp));

        uploadSkinUniforms(nodeGlobal,
                           (nodeIdx < (int)nodeSkin.size() ? nodeSkin[nodeIdx] : -1),
                           globals);

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

    std::function<void(int, const glm::mat4&)> dfsDraw = [&](int node, const glm::mat4& parent) {
        if (node < 0 || node >= (int)locals.size()) return;

        glm::mat4 localM  = trsToMat4(locals[node]);
        glm::mat4 globalM = parent * localM;

        drawMeshAtNode(node, globalM);

        if (node < (int)nodeChildren.size()) {
            for (int c : nodeChildren[node]) dfsDraw(c, globalM);
        }
    };

    if (sceneRoots.empty()) {
        dfsDraw(0, glm::mat4(1.0f));
    } else {
        for (int r : sceneRoots) dfsDraw(r, glm::mat4(1.0f));
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

    // --- Restore previous GL state (critical: UI relies on these) ---
    glDepthMask(prevDepthWrite);

    if (prevBlendEnabled) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);

    glBlendFuncSeparate(prevSrcRGB, prevDstRGB, prevSrcA, prevDstA);
    glBlendEquationSeparate(prevEqRGB, prevEqA);

    if (prevCullEnabled) glEnable(GL_CULL_FACE);
    else glDisable(GL_CULL_FACE);

    glActiveTexture((GLenum)prevActiveTex);

    glBindVertexArray(0);
}
