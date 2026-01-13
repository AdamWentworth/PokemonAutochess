// Model.cpp

#include "Model.h"

#include <glad/glad.h>
#include <iostream>
#include <vector>
#include <limits>
#include <algorithm>
#include <cmath>
#include <functional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../utils/ShaderLibrary.h"

#include <nlohmann/json.hpp>

// Step 4: loader toggle plumbing (fastgltf parse + fallback; no rendering changes yet)
#include "./FastGLTFLoader.h"

// tinygltf implementation MUST live in exactly one .cpp
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_JSON
#include <tinygltf/tiny_gltf.h>

#ifndef PAC_VERBOSE_STARTUP
#define PAC_VERBOSE_STARTUP 0
#endif

#if PAC_VERBOSE_STARTUP
    #define STARTUP_LOG(msg) do { std::cout << msg << "\n"; } while(0)
#else
    #define STARTUP_LOG(msg) do {} while(0)
#endif

bool isMipmapMinFilter(GLint minF);

#include "ModelCache.h"

namespace fs = std::filesystem;

// ------------------------------------------------------------
// Robust accessor helpers (file-local; header does not depend on tinygltf)
// ------------------------------------------------------------
namespace {

const unsigned char* accessorDataPtr(const tinygltf::Model& m,
                                     const tinygltf::Accessor& a)
{
    if (a.bufferView < 0 || a.bufferView >= (int)m.bufferViews.size()) return nullptr;
    const auto& bv = m.bufferViews[a.bufferView];
    if (bv.buffer < 0 || bv.buffer >= (int)m.buffers.size()) return nullptr;
    const auto& buf = m.buffers[bv.buffer];

    size_t offset = bv.byteOffset + a.byteOffset;
    if (offset >= buf.data.size()) return nullptr;
    return buf.data.data() + offset;
}

size_t accessorStride(const tinygltf::Model& m,
                      const tinygltf::Accessor& a)
{
    if (a.bufferView < 0 || a.bufferView >= (int)m.bufferViews.size()) return 0;
    const auto& bv = m.bufferViews[a.bufferView];

    size_t stride = a.ByteStride(bv);
    if (stride == 0) {
        stride = tinygltf::GetComponentSizeInBytes(a.componentType) *
                 tinygltf::GetNumComponentsInType(a.type);
    }
    return stride;
}

void readAccessorScalarFloat(const tinygltf::Model& m,
                             const tinygltf::Accessor& a,
                             std::vector<float>& out)
{
    out.clear();
    if (a.type != TINYGLTF_TYPE_SCALAR) return;
    if (a.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return;

    const unsigned char* base = accessorDataPtr(m, a);
    if (!base) return;

    size_t stride = accessorStride(m, a);
    out.resize(a.count);

    for (size_t i = 0; i < a.count; ++i) {
        const float* p = reinterpret_cast<const float*>(base + i * stride);
        out[i] = p[0];
    }
}

void readAccessorVec4FloatLike(const tinygltf::Model& m,
                               const tinygltf::Accessor& a,
                               std::vector<glm::vec4>& out)
{
    out.clear();

    const unsigned char* base = accessorDataPtr(m, a);
    if (!base) return;

    size_t stride = accessorStride(m, a);
    int comps = tinygltf::GetNumComponentsInType(a.type);
    if (comps <= 0) return;

    out.resize(a.count);

    if (a.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
        for (size_t i = 0; i < a.count; ++i) {
            const float* p = reinterpret_cast<const float*>(base + i * stride);
            glm::vec4 v(0.0f);
            v.x = (comps > 0) ? p[0] : 0.0f;
            v.y = (comps > 1) ? p[1] : 0.0f;
            v.z = (comps > 2) ? p[2] : 0.0f;
            v.w = (comps > 3) ? p[3] : 0.0f;
            out[i] = v;
        }
        return;
    }

    auto readNorm = [&](size_t idx, int c)->float{
        const unsigned char* ptr = base + idx * stride;
        if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(ptr);
            return (c < comps) ? (float(p[c]) / 255.0f) : 0.0f;
        }
        if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            const uint16_t* p = reinterpret_cast<const uint16_t*>(ptr);
            return (c < comps) ? (float(p[c]) / 65535.0f) : 0.0f;
        }
        return 0.0f;
    };

    for (size_t i = 0; i < a.count; ++i) {
        glm::vec4 v(0.0f);
        v.x = readNorm(i,0);
        v.y = readNorm(i,1);
        v.z = readNorm(i,2);
        v.w = readNorm(i,3);
        out[i] = v;
    }
}

}

bool isMipmapMinFilter(GLint minF) {
    return minF == GL_NEAREST_MIPMAP_NEAREST ||
           minF == GL_LINEAR_MIPMAP_NEAREST ||
           minF == GL_NEAREST_MIPMAP_LINEAR ||
           minF == GL_LINEAR_MIPMAP_LINEAR;
}

// ------------------------------------------------------------
// TRS helper
// ------------------------------------------------------------
glm::mat4 Model::trsToMat4(const NodeTRS& n)
{
    if (n.hasMatrix) return n.matrix;

    glm::mat4 T = glm::translate(glm::mat4(1.0f), n.t);
    glm::mat4 R = glm::toMat4(n.r);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), n.s);
    return T * R * S;
}

Model::Model(const std::string& filepath)
{
    loadGLTF(filepath);

    // shared cached shader
    modelShader = ShaderLibrary::get("assets/shaders/model/model.vert", "assets/shaders/model/model.frag");

    locMVP     = glGetUniformLocation(modelShader->getID(), "u_MVP");
    locUseSkin = glGetUniformLocation(modelShader->getID(), "u_UseSkin");
    locJoints0 = glGetUniformLocation(modelShader->getID(), "u_Joints[0]");

    if (locMVP < 0)     std::cerr << "[Model] WARNING: u_MVP not found\n";
    if (locUseSkin < 0) std::cerr << "[Model] WARNING: u_UseSkin not found\n";
    if (locJoints0 < 0) std::cerr << "[Model] WARNING: u_Joints[0] not found\n";
}

Model::~Model()
{
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (EBO) glDeleteBuffers(1, &EBO);

    for (auto& sm : submeshes) {
        if (sm.textureID) glDeleteTextures(1, &sm.textureID);
    }

    modelShader.reset();
}

int Model::getAnimationCount() const
{
    return static_cast<int>(animations.size());
}

float Model::getAnimationDurationSec(int animIndex) const
{
    if (animIndex < 0 || animIndex >= (int)animations.size()) return 0.0f;
    return animations[animIndex].durationSec;
}

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

    bool hasNodeMesh = false;
    for (int m : nodeMesh) {
        if (m >= 0) { hasNodeMesh = true; break; }
    }

    if (!hasNodeMesh) {
        glm::mat4 mvp = camera.getProjectionMatrix() * camera.getViewMatrix() * instanceTransform;
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform1i(locUseSkin, 0);

        for (const auto& sm : submeshes) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sm.textureID);
            glDrawElements(GL_TRIANGLES,
                           (GLsizei)sm.indexCount,
                           GL_UNSIGNED_INT,
                           (void*)(sm.indexOffset * sizeof(uint32_t)));
        }
        glBindVertexArray(0);
        return;
    }

    for (int nodeIdx = 0; nodeIdx < (int)nodeMesh.size(); ++nodeIdx) {
        int meshIdx = nodeMesh[nodeIdx];
        if (meshIdx < 0) continue;

        glm::mat4 meshGlobal = (nodeIdx < (int)globals.size()) ? globals[nodeIdx] : glm::mat4(1.0f);
        glm::mat4 modelMat = instanceTransform * meshGlobal;
        glm::mat4 mvp = camera.getProjectionMatrix() * camera.getViewMatrix() * modelMat;

        glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(mvp));

        int skinIdx = (nodeIdx < (int)nodeSkin.size()) ? nodeSkin[nodeIdx] : -1;
        if (skinIdx >= 0 && locJoints0 >= 0 && locUseSkin >= 0) {
            uploadSkinUniforms(meshGlobal, skinIdx, globals);
        } else {
            glUniform1i(locUseSkin, 0);
        }

        for (const auto& sm : submeshes) {
            if (sm.meshIndex != meshIdx) continue;

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sm.textureID);

            glDrawElements(GL_TRIANGLES,
                           (GLsizei)sm.indexCount,
                           GL_UNSIGNED_INT,
                           (void*)(sm.indexOffset * sizeof(uint32_t)));
        }
    }

    glBindVertexArray(0);
}

// ------------------------------------------------------------
// Load glTF: geometry + nodes + skins + animations
// ------------------------------------------------------------
static glm::quat nodeDefaultQuat(const tinygltf::Node& n)
{
    if (n.rotation.size() == 4) {
        return glm::normalize(glm::quat(
            (float)n.rotation[3],
            (float)n.rotation[0],
            (float)n.rotation[1],
            (float)n.rotation[2]
        ));
    }
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

void Model::loadGLTF(const std::string& filepath)
{
    // Fast path: cache hit
    if (tryLoadCache(filepath)) {
        return;
    }

    // ------------------------------------------------------------
    // Step 4: Optional fastgltf parse attempt (NO behavior change yet).
    // If it fails, we ignore and proceed with tinygltf.
    // If it succeeds, we still proceed with tinygltf for now.
    // ------------------------------------------------------------
    if (pac::fastgltf_loader::shouldUseFastGLTF()) {
        auto fg = pac::fastgltf_loader::tryLoad(filepath);
        if (!fg.has_value()) {
            std::cerr << "[Model] fastgltf failed; continuing with tinygltf for: " << filepath << "\n";
        } else {
            // Parsed OK. Not used yet.
        }
    }

    tinygltf::Model gltf;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok = loader.LoadBinaryFromFile(&gltf, &err, &warn, filepath);
    if (!warn.empty()) std::cout << "[GLTF] Warning: " << warn << "\n";
    if (!err.empty())  std::cerr << "[GLTF] Error: " << err << "\n";
    if (!ok) {
        std::cerr << "[GLTF] Failed to load: " << filepath << "\n";
        return;
    }

    nodesDefault.clear();
    nodeChildren.clear();
    nodeMesh.clear();
    nodeSkin.clear();
    sceneRoots.clear();

    nodesDefault.resize(gltf.nodes.size());
    nodeChildren.resize(gltf.nodes.size());
    nodeMesh.assign(gltf.nodes.size(), -1);
    nodeSkin.assign(gltf.nodes.size(), -1);

    for (size_t i = 0; i < gltf.nodes.size(); ++i) {
        const auto& n = gltf.nodes[i];

        NodeTRS trs;
        if (n.matrix.size() == 16) {
            trs.hasMatrix = true;
            trs.matrix = glm::make_mat4(n.matrix.data());
        } else {
            if (n.translation.size() == 3) trs.t = glm::vec3((float)n.translation[0], (float)n.translation[1], (float)n.translation[2]);
            trs.r = nodeDefaultQuat(n);
            if (n.scale.size() == 3) trs.s = glm::vec3((float)n.scale[0], (float)n.scale[1], (float)n.scale[2]);
        }
        nodesDefault[i] = trs;

        nodeChildren[i] = n.children;

        if (n.mesh >= 0) nodeMesh[i] = n.mesh;
        if (n.skin >= 0) nodeSkin[i] = n.skin;
    }

    if (!gltf.scenes.empty()) {
        int sceneIndex = gltf.defaultScene >= 0 ? gltf.defaultScene : 0;
        sceneIndex = std::max(0, std::min(sceneIndex, (int)gltf.scenes.size() - 1));
        sceneRoots = gltf.scenes[sceneIndex].nodes;
    }

    skins.clear();
    skins.resize(gltf.skins.size());

    for (size_t si = 0; si < gltf.skins.size(); ++si) {
        const auto& s = gltf.skins[si];
        SkinData out;
        out.joints = s.joints;

        if (s.inverseBindMatrices >= 0 && s.inverseBindMatrices < (int)gltf.accessors.size()) {
            const auto& a = gltf.accessors[s.inverseBindMatrices];
            const unsigned char* base = accessorDataPtr(gltf, a);
            size_t stride = accessorStride(gltf, a);

            if (base && a.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && a.type == TINYGLTF_TYPE_MAT4) {
                out.inverseBind.resize(a.count, glm::mat4(1.0f));
                for (size_t i = 0; i < a.count; ++i) {
                    const float* p = reinterpret_cast<const float*>(base + i * stride);
                    out.inverseBind[i] = glm::make_mat4(p);
                }
            }
        }

        if (out.inverseBind.size() != out.joints.size()) {
            out.inverseBind.assign(out.joints.size(), glm::mat4(1.0f));
        }

        skins[si] = std::move(out);
    }

    animations.clear();
    animations.reserve(gltf.animations.size());

    for (const auto& anim : gltf.animations) {
        AnimationClip clip;
        clip.name = anim.name;

        clip.samplers.resize(anim.samplers.size());
        for (size_t si = 0; si < anim.samplers.size(); ++si) {
            const auto& s = anim.samplers[si];
            AnimationSampler samp;
            samp.interpolation = s.interpolation.empty() ? "LINEAR" : s.interpolation;

            if (s.input >= 0 && s.input < (int)gltf.accessors.size()) {
                readAccessorScalarFloat(gltf, gltf.accessors[s.input], samp.inputs);
            }

            if (s.output >= 0 && s.output < (int)gltf.accessors.size()) {
                const auto& outAcc = gltf.accessors[s.output];
                std::vector<glm::vec4> raw;
                readAccessorVec4FloatLike(gltf, outAcc, raw);

                if (samp.interpolation == "CUBICSPLINE" && !samp.inputs.empty()) {
                    size_t keys = samp.inputs.size();
                    std::vector<glm::vec4> values;
                    values.reserve(keys);
                    for (size_t k = 0; k < keys; ++k) {
                        size_t idx = k * 3 + 1;
                        if (idx < raw.size()) values.push_back(raw[idx]);
                    }
                    samp.outputs = std::move(values);
                } else {
                    samp.outputs = std::move(raw);
                }

                samp.isVec4 = (outAcc.type == TINYGLTF_TYPE_VEC4);
            }

            if (!samp.inputs.empty()) {
                clip.durationSec = std::max(clip.durationSec, samp.inputs.back());
            }

            clip.samplers[si] = std::move(samp);
        }

        clip.channels.reserve(anim.channels.size());
        for (const auto& ch : anim.channels) {
            AnimationChannel c;
            c.samplerIndex = ch.sampler;
            c.targetNode   = ch.target_node;

            if (ch.target_path == "translation") c.path = ChannelPath::Translation;
            else if (ch.target_path == "rotation") c.path = ChannelPath::Rotation;
            else if (ch.target_path == "scale") c.path = ChannelPath::Scale;
            else continue;

            clip.channels.push_back(c);
        }

        animations.push_back(std::move(clip));
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(20000);
    indices.reserve(60000);

    submeshes.clear();
    std::vector<CPUTexture> texturesCPU;
    texturesCPU.reserve(64);

    auto readVec2Float = [&](int accessorIndex, std::vector<glm::vec2>& out) {
        out.clear();
        if (accessorIndex < 0 || accessorIndex >= (int)gltf.accessors.size()) return;
        const auto& a = gltf.accessors[accessorIndex];
        if (a.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return;
        if (a.type != TINYGLTF_TYPE_VEC2) return;

        const unsigned char* base = accessorDataPtr(gltf, a);
        if (!base) return;
        size_t stride = accessorStride(gltf, a);

        out.resize(a.count);
        for (size_t i = 0; i < a.count; ++i) {
            const float* p = reinterpret_cast<const float*>(base + i * stride);
            out[i] = glm::vec2(p[0], p[1]);
        }
    };

    auto readVec3Float = [&](int accessorIndex, std::vector<glm::vec3>& out) {
        out.clear();
        if (accessorIndex < 0 || accessorIndex >= (int)gltf.accessors.size()) return;
        const auto& a = gltf.accessors[accessorIndex];
        if (a.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return;
        if (a.type != TINYGLTF_TYPE_VEC3) return;

        const unsigned char* base = accessorDataPtr(gltf, a);
        if (!base) return;
        size_t stride = accessorStride(gltf, a);

        out.resize(a.count);
        for (size_t i = 0; i < a.count; ++i) {
            const float* p = reinterpret_cast<const float*>(base + i * stride);
            out[i] = glm::vec3(p[0], p[1], p[2]);
        }
    };

    auto readJoints4U16 = [&](int accessorIndex, std::vector<glm::u16vec4>& out) {
        out.clear();
        if (accessorIndex < 0 || accessorIndex >= (int)gltf.accessors.size()) return;
        const auto& a = gltf.accessors[accessorIndex];
        if (a.type != TINYGLTF_TYPE_VEC4) return;

        const unsigned char* base = accessorDataPtr(gltf, a);
        if (!base) return;
        size_t stride = accessorStride(gltf, a);

        out.resize(a.count, glm::u16vec4(0));

        if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            for (size_t i = 0; i < a.count; ++i) {
                const uint16_t* p = reinterpret_cast<const uint16_t*>(base + i * stride);
                out[i] = glm::u16vec4(p[0], p[1], p[2], p[3]);
            }
            return;
        }

        if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            for (size_t i = 0; i < a.count; ++i) {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(base + i * stride);
                out[i] = glm::u16vec4(p[0], p[1], p[2], p[3]);
            }
            return;
        }
    };

    auto readWeights4Float = [&](int accessorIndex, std::vector<glm::vec4>& out) {
        out.clear();
        if (accessorIndex < 0 || accessorIndex >= (int)gltf.accessors.size()) return;
        const auto& a = gltf.accessors[accessorIndex];
        if (a.type != TINYGLTF_TYPE_VEC4) return;

        const unsigned char* base = accessorDataPtr(gltf, a);
        if (!base) return;
        size_t stride = accessorStride(gltf, a);

        out.resize(a.count, glm::vec4(1,0,0,0));

        if (a.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            for (size_t i = 0; i < a.count; ++i) {
                const float* p = reinterpret_cast<const float*>(base + i * stride);
                out[i] = glm::vec4(p[0], p[1], p[2], p[3]);
            }
            return;
        }

        if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            for (size_t i = 0; i < a.count; ++i) {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(base + i * stride);
                out[i] = glm::vec4(p[0]/255.0f, p[1]/255.0f, p[2]/255.0f, p[3]/255.0f);
            }
            return;
        }
        if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            for (size_t i = 0; i < a.count; ++i) {
                const uint16_t* p = reinterpret_cast<const uint16_t*>(base + i * stride);
                out[i] = glm::vec4(p[0]/65535.0f, p[1]/65535.0f, p[2]/65535.0f, p[3]/65535.0f);
            }
            return;
        }
    };

    auto readIndicesU32 = [&](int accessorIndex, std::vector<uint32_t>& out) {
        out.clear();
        if (accessorIndex < 0 || accessorIndex >= (int)gltf.accessors.size()) return;
        const auto& a = gltf.accessors[accessorIndex];
        if (a.type != TINYGLTF_TYPE_SCALAR) return;

        const unsigned char* base = accessorDataPtr(gltf, a);
        if (!base) return;
        size_t stride = accessorStride(gltf, a);

        out.resize(a.count);

        if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            for (size_t i = 0; i < a.count; ++i) {
                const uint16_t* p = reinterpret_cast<const uint16_t*>(base + i * stride);
                out[i] = (uint32_t)p[0];
            }
            return;
        }
        if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            for (size_t i = 0; i < a.count; ++i) {
                const uint32_t* p = reinterpret_cast<const uint32_t*>(base + i * stride);
                out[i] = p[0];
            }
            return;
        }
        if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            for (size_t i = 0; i < a.count; ++i) {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(base + i * stride);
                out[i] = (uint32_t)p[0];
            }
            return;
        }
    };

    auto decodeTextureForMaterial = [&](int materialIndex)->CPUTexture {
        CPUTexture outTex;

        auto makeWhite = [&]() -> CPUTexture {
            CPUTexture t;
            t.width = 1; t.height = 1;
            t.wrapS = GL_REPEAT;
            t.wrapT = GL_REPEAT;
            t.minF  = GL_LINEAR;
            t.magF  = GL_LINEAR;
            t.rgba = {255,255,255,255};
            return t;
        };

        if (materialIndex < 0 || materialIndex >= (int)gltf.materials.size()) {
            return makeWhite();
        }

        const auto& mat = gltf.materials[materialIndex];

        int texIndex = -1;
        if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
            texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
        } else {
            return makeWhite();
        }

        if (texIndex < 0 || texIndex >= (int)gltf.textures.size()) {
            return makeWhite();
        }

        const auto& gt = gltf.textures[texIndex];
        int imgIndex = gt.source;

        if (imgIndex < 0 || imgIndex >= (int)gltf.images.size()) {
            return makeWhite();
        }

        const auto& img = gltf.images[imgIndex];

        // Defaults: glTF default wrap is REPEAT
        GLint wrapS = GL_REPEAT;
        GLint wrapT = GL_REPEAT;
        GLint minF  = GL_LINEAR_MIPMAP_LINEAR;
        GLint magF  = GL_LINEAR;

        if (gt.sampler >= 0 && gt.sampler < (int)gltf.samplers.size()) {
            const auto& s = gltf.samplers[gt.sampler];

            if (s.wrapS == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE) wrapS = GL_CLAMP_TO_EDGE;
            else if (s.wrapS == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT) wrapS = GL_MIRRORED_REPEAT;

            if (s.wrapT == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE) wrapT = GL_CLAMP_TO_EDGE;
            else if (s.wrapT == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT) wrapT = GL_MIRRORED_REPEAT;

            auto mapMin = [&](int f)->GLint {
                switch (f) {
                    case TINYGLTF_TEXTURE_FILTER_NEAREST: return GL_NEAREST;
                    case TINYGLTF_TEXTURE_FILTER_LINEAR: return GL_LINEAR;
                    case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST: return GL_NEAREST_MIPMAP_NEAREST;
                    case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST: return GL_LINEAR_MIPMAP_NEAREST;
                    case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR: return GL_NEAREST_MIPMAP_LINEAR;
                    case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR: return GL_LINEAR_MIPMAP_LINEAR;
                    default: return GL_LINEAR_MIPMAP_LINEAR;
                }
            };
            auto mapMag = [&](int f)->GLint {
                switch (f) {
                    case TINYGLTF_TEXTURE_FILTER_NEAREST: return GL_NEAREST;
                    case TINYGLTF_TEXTURE_FILTER_LINEAR: return GL_LINEAR;
                    default: return GL_LINEAR;
                }
            };

            if (s.minFilter > 0) minF = mapMin(s.minFilter);
            if (s.magFilter > 0) magF = mapMag(s.magFilter);
        }

        outTex.width  = (uint32_t)img.width;
        outTex.height = (uint32_t)img.height;
        outTex.wrapS  = wrapS;
        outTex.wrapT  = wrapT;
        outTex.minF   = minF;
        outTex.magF   = magF;

        // Convert to RGBA8 always
        if (img.width <= 0 || img.height <= 0 || img.image.empty()) {
            return makeWhite();
        }

        const int comp = img.component; // 3 or 4 typical
        const size_t pxCount = (size_t)img.width * (size_t)img.height;

        outTex.rgba.resize(pxCount * 4);

        if (comp == 4) {
            // Already RGBA
            const size_t need = pxCount * 4;
            if (img.image.size() >= need) {
                std::memcpy(outTex.rgba.data(), img.image.data(), need);
            } else {
                // defensive fallback
                return makeWhite();
            }
        } else if (comp == 3) {
            const size_t need = pxCount * 3;
            if (img.image.size() >= need) {
                for (size_t i = 0; i < pxCount; ++i) {
                    outTex.rgba[i*4+0] = img.image[i*3+0];
                    outTex.rgba[i*4+1] = img.image[i*3+1];
                    outTex.rgba[i*4+2] = img.image[i*3+2];
                    outTex.rgba[i*4+3] = 255;
                }
            } else {
                return makeWhite();
            }
        } else {
            return makeWhite();
        }

        return outTex;
    };

    auto createGLTextureFromCPU = [&](const CPUTexture& t)->GLuint {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        const uint32_t w = (t.width  == 0 ? 1u : t.width);
        const uint32_t h = (t.height == 0 ? 1u : t.height);

        const void* pixels = t.rgba.empty() ? nullptr : t.rgba.data();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)t.wrapS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)t.wrapT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)t.minF);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)t.magF);

        if (isMipmapMinFilter((GLint)t.minF)) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }

        return tex;
    };

    float minX = std::numeric_limits<float>::max(), minY = std::numeric_limits<float>::max(), minZ = std::numeric_limits<float>::max();
    float maxX = -minX, maxY = -minY, maxZ = -minZ;

    for (int meshIdx = 0; meshIdx < (int)gltf.meshes.size(); ++meshIdx) {
        const auto& mesh = gltf.meshes[meshIdx];

        for (int primIdx = 0; primIdx < (int)mesh.primitives.size(); ++primIdx) {
            const auto& prim = mesh.primitives[primIdx];
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;

            auto itPos = prim.attributes.find("POSITION");
            auto itUv  = prim.attributes.find("TEXCOORD_0");
            if (itPos == prim.attributes.end() || itUv == prim.attributes.end()) {
                std::cerr << "[GLTF] Missing POSITION or TEXCOORD_0 in primitive\n";
                continue;
            }

            std::vector<glm::vec3> pos;
            std::vector<glm::vec2> uv;
            readVec3Float(itPos->second, pos);
            readVec2Float(itUv->second,  uv);

            if (pos.empty() || uv.empty() || pos.size() != uv.size()) {
                std::cerr << "[GLTF] Invalid POSITION/TEXCOORD_0 sizes\n";
                continue;
            }

            std::vector<glm::u16vec4> joints;
            std::vector<glm::vec4> weights;

            auto itJ = prim.attributes.find("JOINTS_0");
            auto itW = prim.attributes.find("WEIGHTS_0");
            if (itJ != prim.attributes.end() && itW != prim.attributes.end()) {
                readJoints4U16(itJ->second, joints);
                readWeights4Float(itW->second, weights);

                if (joints.size() != pos.size() || weights.size() != pos.size()) {
                    joints.clear();
                    weights.clear();
                }
            }

            std::vector<uint32_t> primIdxU32;
            if (prim.indices >= 0) {
                readIndicesU32(prim.indices, primIdxU32);
            }
            if (primIdxU32.empty()) {
                primIdxU32.resize(pos.size());
                for (size_t i = 0; i < pos.size(); ++i) primIdxU32[i] = (uint32_t)i;
            }

            size_t baseVertex = vertices.size();
            size_t subIndexOffset = indices.size();

            for (size_t i = 0; i < pos.size(); ++i) {
                Vertex v{};
                v.px = pos[i].x; v.py = pos[i].y; v.pz = pos[i].z;
                v.u  = uv[i].x;  v.v  = uv[i].y;

                v.j0 = v.j1 = v.j2 = v.j3 = 0;
                v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f;

                if (!joints.empty() && !weights.empty()) {
                    auto j = joints[i];
                    auto w = weights[i];

                    float sum = w.x + w.y + w.z + w.w;
                    if (sum <= 0.0001f) {
                        w = glm::vec4(1,0,0,0);
                    } else {
                        w /= sum;
                    }

                    v.j0 = j.x; v.j1 = j.y; v.j2 = j.z; v.j3 = j.w;
                    v.w0 = w.x; v.w1 = w.y; v.w2 = w.z; v.w3 = w.w;
                }

                vertices.push_back(v);

                minX = std::min(minX, v.px); minY = std::min(minY, v.py); minZ = std::min(minZ, v.pz);
                maxX = std::max(maxX, v.px); maxY = std::max(maxY, v.py); maxZ = std::max(maxZ, v.pz);
            }

            for (auto idx : primIdxU32) {
                indices.push_back((uint32_t)(baseVertex + idx));
            }

            CPUTexture cpuTex = decodeTextureForMaterial(prim.material);
            GLuint texId = createGLTextureFromCPU(cpuTex);

            Submesh sm;
            sm.indexOffset = subIndexOffset;
            sm.indexCount  = primIdxU32.size();
            sm.textureID   = texId;
            sm.meshIndex   = meshIdx;
            submeshes.push_back(sm);

            texturesCPU.push_back(std::move(cpuTex));
        }
    }

    float desiredHeight = 0.8f;
    float denom = (maxZ - minZ);
    if (std::abs(denom) < 1e-6f) denom = 1.0f;
    modelScaleFactor = desiredHeight / denom;

    STARTUP_LOG(
        std::string("[Model] Loaded: ") + filepath +
        " vertices=" + std::to_string(vertices.size()) +
        " indices=" + std::to_string(indices.size()) +
        " submeshes=" + std::to_string(submeshes.size()) +
        " animations=" + std::to_string(animations.size()) +
        " skins=" + std::to_string(skins.size())
    );

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, px));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(1);

    glVertexAttribIPointer(2, 4, GL_UNSIGNED_SHORT, sizeof(Vertex), (void*)offsetof(Vertex, j0));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, w0));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);

    // Persist processed result for faster next startup
    writeCache(filepath, vertices, indices, texturesCPU);
}
