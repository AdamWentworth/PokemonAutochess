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

#include "ModelAnimationTypes.h"
#include "ModelMeshTypes.h"

struct Submesh {
    size_t indexOffset = 0;
    size_t indexCount = 0;
    unsigned int baseColorTexID = 0;
    unsigned int emissiveTexID  = 0;
    glm::vec3 emissiveFactor{0.0f};
    int alphaMode = 0; // 0=OPAQUE, 1=MASK, 2=BLEND
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
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

    // CPU-side texture blob for caching (always RGBA8)
    struct CPUTexture {
        uint32_t width = 1;
        uint32_t height = 1;
        int wrapS = 0;   // GL enum stored as int
        int wrapT = 0;   // GL enum stored as int
        int minF  = 0;   // GL enum stored as int
        int magF  = 0;   // GL enum stored as int
        std::vector<uint8_t> rgba; // width*height*4
    };

private:
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    std::vector<Submesh> submeshes;

    std::shared_ptr<Shader> modelShader;
    int locMVP = -1;
    int locUseSkin = -1;
    int locJoints0 = -1;
    int locBaseColorTex = -1;
    int locEmissiveTex  = -1;
    int locEmissiveFactor = -1;
    int locAlphaMode = -1;
    int locAlphaCutoff = -1;

    float modelScaleFactor = 1.0f;

    using NodeTRS          = pac_model_types::NodeTRS;
    using SkinData         = pac_model_types::SkinData;
    using ChannelPath      = pac_model_types::ChannelPath;
    using AnimationSampler = pac_model_types::AnimationSampler;
    using AnimationChannel = pac_model_types::AnimationChannel;
    using AnimationClip    = pac_model_types::AnimationClip;

    using Vertex           = pac_model_types::Vertex;

    std::vector<NodeTRS>           nodesDefault;
    std::vector<std::vector<int>>  nodeChildren;
    std::vector<int>               nodeMesh;
    std::vector<int>               nodeSkin;
    std::vector<int>               sceneRoots;
    std::vector<SkinData>          skins;
    std::vector<AnimationClip>     animations;

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

    // ---- On-disk cache helpers ----
    bool tryLoadCache(const std::string& filepath);
    void writeCache(const std::string& filepath,
                    const std::vector<Vertex>& vertices,
                    const std::vector<uint32_t>& indices,
                    const std::vector<CPUTexture>& baseColorTexturesCPU,
                    const std::vector<CPUTexture>& emissiveTexturesCPU) const;
};
