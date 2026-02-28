#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/IRenderBackend.h"

namespace game::runtime::shared_world_batches {

struct WorldIndexedBatch {
    std::vector<IRenderBackend::WorldMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::string textureKey;
    std::vector<unsigned char> ownedTextureRgba;
    const unsigned char* textureRgba = nullptr;
    int textureWidth = 0;
    int textureHeight = 0;
    int textureWrapS = 10497;
    int textureWrapT = 10497;
    std::uint8_t alphaMode = 0u;
    std::uint8_t blendMode = 0u;
    std::uint8_t materialMode = 0u;
    float alphaCutoff = 0.5f;
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
    std::vector<float> skinMatrices;
};

void submitWorldIndexedBatches(IRenderBackend& renderer,
                               const std::vector<WorldIndexedBatch>& batches,
                               const float* viewProjectionMatrix4x4,
                               int surfaceWidth,
                               int surfaceHeight);

} // namespace game::runtime::shared_world_batches

