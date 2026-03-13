#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include "engine/render/ModelAnimationTypes.h"

namespace game::runtime::render_model {

struct MeshVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 tangent{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 color{1.0f};
    std::uint16_t j0 = 0u;
    std::uint16_t j1 = 0u;
    std::uint16_t j2 = 0u;
    std::uint16_t j3 = 0u;
    float w0 = 0.0f;
    float w1 = 0.0f;
    float w2 = 0.0f;
    float w3 = 0.0f;
};

struct CachedTextureRgba {
    int width = 0;
    int height = 0;
    int wrapS = 10497;
    int wrapT = 10497;
    int minF = 9729;
    int magF = 9729;
    std::vector<unsigned char> rgba;

    bool hasPixels() const {
        if (width <= 0 || height <= 0) return false;
        const std::uint64_t pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        const std::uint64_t requiredBytes = pixels * 4ull;
        return requiredBytes > 0ull && requiredBytes <= static_cast<std::uint64_t>(rgba.size());
    }
};

struct MeshData {
    float modelScaleFactor = 1.0f;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint16_t> triangleSubmesh;
    std::vector<glm::vec3> triangleBaseColors;
    std::vector<float> triangleOpacity;
    std::vector<std::uint8_t> triangleDoubleSided;
    std::vector<glm::vec4> submeshBaseColors;
    std::vector<int> submeshMeshIndex;
    std::vector<std::uint32_t> submeshIndexOffset;
    std::vector<std::uint32_t> submeshIndexCount;
    std::vector<CachedTextureRgba> submeshBaseTextures;
    std::vector<CachedTextureRgba> submeshNormalTextures;
    std::vector<CachedTextureRgba> submeshMetallicRoughnessTextures;
    std::vector<CachedTextureRgba> submeshOcclusionTextures;
    std::vector<CachedTextureRgba> submeshEmissiveTextures;
    std::vector<std::uint8_t> submeshAlphaMode;
    std::vector<float> submeshAlphaCutoff;
    std::vector<float> submeshNormalScale;
    std::vector<float> submeshMetallicFactor;
    std::vector<float> submeshRoughnessFactor;
    std::vector<float> submeshOcclusionStrength;
    std::vector<glm::vec3> submeshEmissiveFactors;
    std::vector<int> meshIndexToNode;
    std::vector<int> triangleNodeIndex;
    std::vector<int> triangleSkinIndex;
    std::vector<glm::vec3> vertexBaseColors;
    std::vector<pac_model_types::NodeTRS> nodesDefault;
    std::vector<std::string> nodeNames;
    std::vector<std::vector<int>> nodeChildren;
    std::vector<int> nodeParent;
    std::vector<int> nodeMesh;
    std::vector<int> nodeSkin;
    std::vector<int> sceneRoots;
    std::vector<glm::mat4> bindNodeGlobals;
    std::vector<pac_model_types::SkinData> skins;
    std::vector<pac_model_types::AnimationClip> animations;
    bool hasVertexColor = false;
    bool hasVertexBaseColor = false;
};

std::string cachePathForModel(const std::string& modelPath);
bool loadMeshFromCache(const std::string& modelPath, MeshData& out, std::string* outError = nullptr);

} // namespace game::runtime::render_model
