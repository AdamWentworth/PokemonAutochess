// src/engine/render/Model.h
#pragma once

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Camera3D.h"
#include "../utils/Shader.h"

struct Submesh {
    size_t indexOffset = 0;
    size_t indexCount = 0;
    unsigned int textureID = 0;
    int meshIndex = -1;
};

class Model {
public:
    explicit Model(const std::string& filepath);
    ~Model();

    int   getAnimationCount() const;
    float getAnimationDurationSec(int animIndex) const;

    void drawAnimated(const Camera3D& camera,
                      const glm::mat4& instanceTransform,
                      float animTimeSec,
                      int animIndex) const;

    float getScaleFactor() const { return modelScaleFactor; }

private:
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    std::vector<Submesh> submeshes;

    // NEW: shared shader program (no per-model compilation)
    std::shared_ptr<Shader> modelShader;
    int locMVP = -1;
    int locUseSkin = -1;
    int locJoints0 = -1;

    float modelScaleFactor = 1.0f;

    struct NodeTRS {
        glm::vec3 t{0.0f};
        glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 s{1.0f};
        bool hasMatrix = false;
        glm::mat4 matrix{1.0f};
    };

    struct SkinData {
        std::vector<int> joints;
        std::vector<glm::mat4> inverseBind;
    };

    enum class ChannelPath { Translation, Rotation, Scale };

    struct AnimationSampler {
        std::vector<float> inputs;
        std::vector<glm::vec4> outputs;
        std::string interpolation;
        bool isVec4 = false;
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

    std::vector<NodeTRS>           nodesDefault;
    std::vector<std::vector<int>>  nodeChildren;
    std::vector<int>              nodeMesh;
    std::vector<int>              nodeSkin;
    std::vector<int>              sceneRoots;
    std::vector<SkinData>         skins;
    std::vector<AnimationClip>    animations;

    struct Vertex {
        float px, py, pz;
        float u, v;
        uint16_t j0, j1, j2, j3;
        float w0, w1, w2, w3;
    };

    mutable std::unordered_set<int> warnedMissingAnimIndex;

    void loadGLTF(const std::string& filepath);

    static glm::mat4 trsToMat4(const NodeTRS& n);
    void buildPoseMatrices(float timeSec,
                           int animIndex,
                           std::vector<NodeTRS>& outLocal,
                           std::vector<glm::mat4>& outGlobal) const;

    void uploadSkinUniforms(const glm::mat4& meshGlobal,
                            int skinIndex,
                            const std::vector<glm::mat4>& nodeGlobals) const;
};
