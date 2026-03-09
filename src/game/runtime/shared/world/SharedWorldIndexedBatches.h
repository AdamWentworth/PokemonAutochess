#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/IRenderBackend.h"

namespace game::runtime::shared_world_batches {

struct WorldIndexedBatch {
    // Optional shared/static material template for reducing per-frame metadata copies.
    // When set, texture/material descriptors are sourced from this template while
    // geometry/model/skin data remain per-batch.
    const WorldIndexedBatch* sharedTemplate = nullptr;
    std::vector<IRenderBackend::WorldMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    // Optional shared/static geometry path for reducing per-frame CPU copies.
    // When set, submit path prefers these pointers over local vectors.
    const IRenderBackend::WorldMeshVertex* sharedVertices = nullptr;
    std::size_t sharedVertexCount = 0u;
    const std::uint32_t* sharedIndices = nullptr;
    std::size_t sharedIndexCount = 0u;
    std::string geometryCacheKey;
    std::string textureKey;
    std::string textureCacheKey;
    std::vector<unsigned char> ownedTextureRgba;
    const unsigned char* textureRgba = nullptr;
    int textureWidth = 0;
    int textureHeight = 0;
    int textureWrapS = 10497;
    int textureWrapT = 10497;
    std::string normalTextureKey;
    std::string normalTextureCacheKey;
    std::vector<unsigned char> ownedNormalTextureRgba;
    const unsigned char* normalTextureRgba = nullptr;
    int normalTextureWidth = 0;
    int normalTextureHeight = 0;
    int normalTextureWrapS = 10497;
    int normalTextureWrapT = 10497;
    std::string metallicRoughnessTextureKey;
    std::string metallicRoughnessTextureCacheKey;
    std::vector<unsigned char> ownedMetallicRoughnessTextureRgba;
    const unsigned char* metallicRoughnessTextureRgba = nullptr;
    int metallicRoughnessTextureWidth = 0;
    int metallicRoughnessTextureHeight = 0;
    int metallicRoughnessTextureWrapS = 10497;
    int metallicRoughnessTextureWrapT = 10497;
    std::string occlusionTextureKey;
    std::string occlusionTextureCacheKey;
    std::vector<unsigned char> ownedOcclusionTextureRgba;
    const unsigned char* occlusionTextureRgba = nullptr;
    int occlusionTextureWidth = 0;
    int occlusionTextureHeight = 0;
    int occlusionTextureWrapS = 10497;
    int occlusionTextureWrapT = 10497;
    std::string emissiveTextureKey;
    std::string emissiveTextureCacheKey;
    std::vector<unsigned char> ownedEmissiveTextureRgba;
    const unsigned char* emissiveTextureRgba = nullptr;
    int emissiveTextureWidth = 0;
    int emissiveTextureHeight = 0;
    int emissiveTextureWrapS = 10497;
    int emissiveTextureWrapT = 10497;
    std::uint8_t alphaMode = 0u;
    std::uint8_t blendMode = 0u;
    std::uint8_t materialMode = 0u;
    bool materialAlphaOverride = false;
    float alphaCutoff = 0.5f;
    float normalScale = 1.0f;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;
    float emissiveFactorR = 0.0f;
    float emissiveFactorG = 0.0f;
    float emissiveFactorB = 0.0f;
    float vertexColorMulR = 1.0f;
    float vertexColorMulG = 1.0f;
    float vertexColorMulB = 1.0f;
    float vertexColorMulA = 1.0f;
    std::uint8_t characterInkingEnabled = 0u;
    float sortDepth = 0.0f;
    float materialTimeSec = 0.0f;
    float materialFlags = 0.0f;
    float materialAtlasWidth = 0.0f;
    float materialAtlasHeight = 0.0f;
    float materialRect0U = 0.0f;
    float materialRect0V = 0.0f;
    float materialRect0W = 1.0f;
    float materialRect0H = 1.0f;
    float materialRect1U = 0.0f;
    float materialRect1V = 0.0f;
    float materialRect1W = 1.0f;
    float materialRect1H = 1.0f;
    float materialFlipbook0Cols = 1.0f;
    float materialFlipbook0Rows = 1.0f;
    float materialFlipbook0Frames = 1.0f;
    float materialFlipbook0Fps = 0.0f;
    float materialFlipbook1Cols = 1.0f;
    float materialFlipbook1Rows = 1.0f;
    float materialFlipbook1Frames = 1.0f;
    float materialFlipbook1Fps = 0.0f;
    std::array<float, 16> modelMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    std::uint8_t gpuSkinning = 0u;
    std::uint32_t skinMatrixCount = 0u;
    const float* sharedSkinMatrices = nullptr;
    std::vector<float> skinMatrices;

    bool hasGeometry() const {
        const bool hasVertices =
            (!vertices.empty()) || (sharedVertices && sharedVertexCount > 0u);
        const bool hasIndices =
            (!indices.empty()) || (sharedIndices && sharedIndexCount > 0u);
        return hasVertices && hasIndices;
    }
};

const WorldIndexedBatch& resolvedMaterialBatch(const WorldIndexedBatch& batch);
bool resolvedHasBaseTexture(const WorldIndexedBatch& batch);
bool resolvedHasNormalTexture(const WorldIndexedBatch& batch);
std::size_t prewarmWorldIndexedBatches(IRenderBackend& renderer,
                                       const std::vector<WorldIndexedBatch>& batches,
                                       const float* cameraWorldPos3 = nullptr,
                                       const float* cameraForward3 = nullptr,
                                       const float* cameraTarget3 = nullptr);

void submitWorldIndexedBatches(IRenderBackend& renderer,
                               const std::vector<WorldIndexedBatch>& batches,
                               const float* viewProjectionMatrix4x4,
                               int surfaceWidth,
                               int surfaceHeight,
                               const float* cameraWorldPos3 = nullptr,
                               const float* cameraForward3 = nullptr,
                               const float* cameraTarget3 = nullptr);

} // namespace game::runtime::shared_world_batches

