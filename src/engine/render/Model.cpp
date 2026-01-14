// src/engine/render/Model.cpp

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
#include <type_traits>
#include <utility>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../utils/ShaderLibrary.h"

#include <nlohmann/json.hpp>

// fastgltf loader
#include "./FastGLTFLoader.h"
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include <stb_image.h>

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

bool isMipmapMinFilter(GLint minF) {
    return minF == GL_NEAREST_MIPMAP_NEAREST ||
           minF == GL_LINEAR_MIPMAP_NEAREST ||
           minF == GL_NEAREST_MIPMAP_LINEAR ||
           minF == GL_LINEAR_MIPMAP_LINEAR;
}

namespace {

// detects o.has_value()
template <typename T, typename = void>
struct fg_has_has_value : std::false_type {};
template <typename T>
struct fg_has_has_value<T, std::void_t<decltype(std::declval<const T&>().has_value())>>
    : std::true_type {};

// detects o.value()  (function)
template <typename T, typename = void>
struct fg_has_value_fn : std::false_type {};
template <typename T>
struct fg_has_value_fn<T, std::void_t<decltype(std::declval<const T&>().value())>>
    : std::true_type {};

// detects o.get() (function)
template <typename T, typename = void>
struct fg_has_get : std::false_type {};
template <typename T>
struct fg_has_get<T, std::void_t<decltype(std::declval<const T&>().get())>>
    : std::true_type {};

// detects o.value (data member)
template <typename T, typename = void>
struct fg_has_value_member : std::false_type {};
template <typename T>
struct fg_has_value_member<T, std::void_t<decltype((std::declval<const T&>().value))>>
    : std::true_type {};

// detects *o
template <typename T, typename = void>
struct fg_is_deref : std::false_type {};
template <typename T>
struct fg_is_deref<T, std::void_t<decltype(*std::declval<const T&>())>>
    : std::true_type {};

template <typename Opt>
bool fgOptHas(const Opt& o) {
    if constexpr (std::is_integral_v<std::decay_t<Opt>> || std::is_enum_v<std::decay_t<Opt>>) {
        return true; // plain value is always "present"
    } else if constexpr (fg_has_has_value<Opt>::value) {
        return o.has_value();
    } else {
        return static_cast<bool>(o);
    }
}

template <typename Opt>
std::size_t fgOptGet(const Opt& o) {
    // if it's already an integer (or enum), just return it
    if constexpr (std::is_integral_v<std::decay_t<Opt>> || std::is_enum_v<std::decay_t<Opt>>) {
        return static_cast<std::size_t>(o);
    } else if constexpr (fg_has_get<Opt>::value) {
        return static_cast<std::size_t>(o.get());
    } else if constexpr (fg_has_value_fn<Opt>::value) {
        return static_cast<std::size_t>(o.value());
    } else if constexpr (fg_has_value_member<Opt>::value) {
        return static_cast<std::size_t>(o.value);
    } else if constexpr (fg_is_deref<Opt>::value) {
        return static_cast<std::size_t>(*o);
    } else {
        static_assert(!sizeof(Opt), "fgOptGet: unsupported optional type");
    }
}

} // namespace

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

    auto drawMeshAtNode = [&](int nodeIdx, const glm::mat4& nodeGlobal) {
        if (nodeIdx < 0 || nodeIdx >= (int)nodeMesh.size()) return;
        int meshIdx = nodeMesh[nodeIdx];
        if (meshIdx < 0) return;

        hasNodeMesh = true;

        glm::mat4 vp  = camera.getProjectionMatrix() * camera.getViewMatrix();
        glm::mat4 mvp = vp * instanceTransform * nodeGlobal;
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(mvp));

        uploadSkinUniforms(nodeGlobal, (nodeIdx < (int)nodeSkin.size() ? nodeSkin[nodeIdx] : -1), globals);

        for (const auto& sm : submeshes) {
            if (sm.meshIndex != meshIdx) continue;

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sm.textureID);

            glDrawElements(GL_TRIANGLES,
                           (GLsizei)sm.indexCount,
                           GL_UNSIGNED_INT,
                           (void*)(sm.indexOffset * sizeof(uint32_t)));
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

    if (!hasNodeMesh) {
        glm::mat4 vp  = camera.getProjectionMatrix() * camera.getViewMatrix();
        glm::mat4 mvp = vp * instanceTransform;
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
    }

    glBindVertexArray(0);
}

// ------------------------------------------------------------
// Load glTF: cache wrapper + fastgltf loader
// ------------------------------------------------------------
void Model::loadGLTF(const std::string& filepath)
{
    if (tryLoadCache(filepath)) {
        std::cerr << "[gltf][CACHE] HIT (no parsing) for: " << filepath << "\n";
        return;
    }
    std::cerr << "[gltf][CACHE] MISS (will parse) for: " << filepath << "\n";

    // Full loader lives here (step 1 modularization)
    #include "ModelFastGltfLoad.inl"
}
