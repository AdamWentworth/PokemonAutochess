// src/engine/render/Model.h
#pragma once

// Enable GLM experimental extensions (required by some glm/gtx/* headers such as component_wise)
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Camera3D.h"
#include "../utils/Shader.h"

struct Submesh {
    size_t indexOffset = 0;      // offset in index buffer (in indices, not bytes)
    size_t indexCount = 0;       // number of indices
    unsigned int textureID = 0;

    int meshIndex = -1;          // which glTF mesh this primitive belongs to
};

class Model {
public:
    explicit Model(const std::string& filepath);
    ~Model();

    // Animation info
    int   getAnimationCount() const;
    float getAnimationDurationSec(int animIndex) const;

    // Main draw entry: draws as static if animation not available.
    // animIndex: request index 1 for 2nd animation; will safely no-op if missing.
    void drawAnimated(const Camera3D& camera,
                      const glm::mat4& instanceTransform,
                      float animTimeSec,
                      int animIndex) const;

    float getScaleFactor() const { return modelScaleFactor; }

private:
    // ---------- GPU data ----------
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    std::vector<Submesh> submeshes;

    Shader* modelShader = nullptr;
    int locMVP = -1;
    int locUseSkin = -1;
    int locJoints0 = -1;

    // ---------- scaling ----------
    float modelScaleFactor = 1.0f;

    // ---------- glTF runtime data ----------
    struct NodeTRS {
        glm::vec3 t{0.0f};
        glm::quat r{1.0f, 0.0f, 0.0f, 0.0f}; // w,x,y,z
        glm::vec3 s{1.0f};
        bool hasMatrix = false;
        glm::mat4 matrix{1.0f}; // if node uses matrix
    };

    struct SkinData {
        std::vector<int> joints;                 // node indices
        std::vector<glm::mat4> inverseBind;      // per joint
    };

    enum class ChannelPath { Translation, Rotation, Scale };

    struct AnimationSampler {
        std::vector<float> inputs;               // key times (seconds)
        std::vector<glm::vec4> outputs;          // vec4 for rotation; vec3 stored in xyz
        std::string interpolation;               // "LINEAR", "STEP", "CUBICSPLINE" (handled safely)
        bool isVec4 = false;                     // rotation = true
    };

    struct AnimationChannel {
        int samplerIndex = -1;
        int targetNode = -1;
        ChannelPath path = ChannelPath::Translation;
    };

    struct AnimationClip {
        std::string name;
        float durationSec = 0.0f;
        std::vector<AnimationSampler> samplers;
        std::vector<AnimationChannel> channels;
    };

    // Stored glTF scene graph data
    std::vector<NodeTRS>           nodesDefault;
    std::vector<std::vector<int>>  nodeChildren;
    std::vector<int>              nodeMesh;        // node -> mesh index (or -1)
    std::vector<int>              nodeSkin;        // node -> skin index (or -1)
    std::vector<int>              sceneRoots;      // nodes from scene 0
    std::vector<SkinData>         skins;
    std::vector<AnimationClip>    animations;

    // Vertex format (packed)
    struct Vertex {
        float px, py, pz;
        float u, v;
        uint16_t j0, j1, j2, j3; // joints
        float w0, w1, w2, w3;    // weights
    };

    // For warning spam control
    mutable std::unordered_set<int> warnedMissingAnimIndex;

    // ---------- loading ----------
    void loadGLTF(const std::string& filepath);

    // ---------- animation evaluation ----------
    static glm::mat4 trsToMat4(const NodeTRS& n);
    void buildPoseMatrices(float timeSec,
                           int animIndex,
                           std::vector<NodeTRS>& outLocal,
                           std::vector<glm::mat4>& outGlobal) const;

    // ---------- drawing helpers ----------
    void uploadSkinUniforms(const glm::mat4& meshGlobal,
                            int skinIndex,
                            const std::vector<glm::mat4>& nodeGlobals) const;
};