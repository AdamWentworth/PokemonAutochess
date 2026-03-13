#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/render/Model.h"
#include "engine/render/ModelAnimationTypes.h"
#include "engine/render/ModelMeshTypes.h"

namespace game::runtime::render_model::detail {

struct SourceSubmeshRecord {
    std::size_t indexOffset = 0u;
    std::size_t indexCount = 0u;
    int meshIndex = -1;
    glm::vec3 emissiveFactor{0.0f};
    float normalScale = 1.0f;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;
    std::uint8_t alphaMode = 0u;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    Model::CPUTexture baseTexture;
    Model::CPUTexture normalTexture;
    Model::CPUTexture metallicRoughnessTexture;
    Model::CPUTexture occlusionTexture;
    Model::CPUTexture emissiveTexture;
};

struct SourceCacheBuildData {
    float modelScaleFactor = 1.0f;
    std::vector<pac_model_types::NodeTRS> nodesDefault;
    std::vector<std::string> nodeNames;
    std::vector<std::vector<int>> nodeChildren;
    std::vector<int> nodeMesh;
    std::vector<int> nodeSkin;
    std::vector<int> sceneRoots;
    std::vector<pac_model_types::SkinData> skins;
    std::vector<pac_model_types::AnimationClip> animations;
    std::vector<pac_model_types::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SourceSubmeshRecord> submeshes;
};

bool buildRenderCacheSourceData(const std::string& filepath,
                                SourceCacheBuildData& outData,
                                std::string* outError);

} // namespace game::runtime::render_model::detail
