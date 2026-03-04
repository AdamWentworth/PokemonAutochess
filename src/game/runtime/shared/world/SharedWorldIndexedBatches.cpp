#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <algorithm>

namespace game::runtime::shared_world_batches {

namespace {

IRenderBackend::WorldTextureData toWorldTextureData(const WorldIndexedBatch& batch,
                                                    const float* cameraWorldPos3,
                                                    const float* cameraForward3,
                                                    const float* cameraTarget3) {
    const unsigned char* rgbaData = batch.textureRgba;
    if (!rgbaData && !batch.ownedTextureRgba.empty()) {
        rgbaData = batch.ownedTextureRgba.data();
    }
    const unsigned char* normalRgbaData = batch.normalTextureRgba;
    if (!normalRgbaData && !batch.ownedNormalTextureRgba.empty()) {
        normalRgbaData = batch.ownedNormalTextureRgba.data();
    }
    const unsigned char* mrRgbaData = batch.metallicRoughnessTextureRgba;
    if (!mrRgbaData && !batch.ownedMetallicRoughnessTextureRgba.empty()) {
        mrRgbaData = batch.ownedMetallicRoughnessTextureRgba.data();
    }
    const unsigned char* occlusionRgbaData = batch.occlusionTextureRgba;
    if (!occlusionRgbaData && !batch.ownedOcclusionTextureRgba.empty()) {
        occlusionRgbaData = batch.ownedOcclusionTextureRgba.data();
    }
    const unsigned char* emissiveRgbaData = batch.emissiveTextureRgba;
    if (!emissiveRgbaData && !batch.ownedEmissiveTextureRgba.empty()) {
        emissiveRgbaData = batch.ownedEmissiveTextureRgba.data();
    }

    IRenderBackend::WorldTextureData tex;
    tex.key = batch.textureKey.c_str();
    tex.rgba = rgbaData;
    tex.width = batch.textureWidth;
    tex.height = batch.textureHeight;
    tex.wrapS = batch.textureWrapS;
    tex.wrapT = batch.textureWrapT;
    tex.normalKey = batch.normalTextureKey.c_str();
    tex.normalRgba = normalRgbaData;
    tex.normalWidth = batch.normalTextureWidth;
    tex.normalHeight = batch.normalTextureHeight;
    tex.normalWrapS = batch.normalTextureWrapS;
    tex.normalWrapT = batch.normalTextureWrapT;
    tex.metallicRoughnessKey = batch.metallicRoughnessTextureKey.c_str();
    tex.metallicRoughnessRgba = mrRgbaData;
    tex.metallicRoughnessWidth = batch.metallicRoughnessTextureWidth;
    tex.metallicRoughnessHeight = batch.metallicRoughnessTextureHeight;
    tex.metallicRoughnessWrapS = batch.metallicRoughnessTextureWrapS;
    tex.metallicRoughnessWrapT = batch.metallicRoughnessTextureWrapT;
    tex.occlusionKey = batch.occlusionTextureKey.c_str();
    tex.occlusionRgba = occlusionRgbaData;
    tex.occlusionWidth = batch.occlusionTextureWidth;
    tex.occlusionHeight = batch.occlusionTextureHeight;
    tex.occlusionWrapS = batch.occlusionTextureWrapS;
    tex.occlusionWrapT = batch.occlusionTextureWrapT;
    tex.emissiveKey = batch.emissiveTextureKey.c_str();
    tex.emissiveRgba = emissiveRgbaData;
    tex.emissiveWidth = batch.emissiveTextureWidth;
    tex.emissiveHeight = batch.emissiveTextureHeight;
    tex.emissiveWrapS = batch.emissiveTextureWrapS;
    tex.emissiveWrapT = batch.emissiveTextureWrapT;
    tex.alphaMode = batch.alphaMode;
    tex.blendMode = batch.blendMode;
    tex.materialMode = batch.materialMode;
    tex.alphaCutoff = batch.alphaCutoff;
    tex.normalScale = batch.normalScale;
    tex.metallicFactor = batch.metallicFactor;
    tex.roughnessFactor = batch.roughnessFactor;
    tex.occlusionStrength = batch.occlusionStrength;
    tex.emissiveFactorR = batch.emissiveFactorR;
    tex.emissiveFactorG = batch.emissiveFactorG;
    tex.emissiveFactorB = batch.emissiveFactorB;
    tex.characterInkingEnabled = batch.characterInkingEnabled;
    tex.cameraPosX = (cameraWorldPos3 ? cameraWorldPos3[0] : tex.cameraPosX);
    tex.cameraPosY = (cameraWorldPos3 ? cameraWorldPos3[1] : tex.cameraPosY);
    tex.cameraPosZ = (cameraWorldPos3 ? cameraWorldPos3[2] : tex.cameraPosZ);
    tex.cameraForwardX = (cameraForward3 ? cameraForward3[0] : tex.cameraForwardX);
    tex.cameraForwardY = (cameraForward3 ? cameraForward3[1] : tex.cameraForwardY);
    tex.cameraForwardZ = (cameraForward3 ? cameraForward3[2] : tex.cameraForwardZ);
    tex.cameraTargetX = (cameraTarget3 ? cameraTarget3[0] : tex.cameraTargetX);
    tex.cameraTargetY = (cameraTarget3 ? cameraTarget3[1] : tex.cameraTargetY);
    tex.cameraTargetZ = (cameraTarget3 ? cameraTarget3[2] : tex.cameraTargetZ);
    tex.materialTimeSec = batch.materialTimeSec;
    tex.materialFlags = batch.materialFlags;
    tex.materialAtlasWidth = batch.materialAtlasWidth;
    tex.materialAtlasHeight = batch.materialAtlasHeight;
    tex.materialRect0U = batch.materialRect0U;
    tex.materialRect0V = batch.materialRect0V;
    tex.materialRect0W = batch.materialRect0W;
    tex.materialRect0H = batch.materialRect0H;
    tex.materialRect1U = batch.materialRect1U;
    tex.materialRect1V = batch.materialRect1V;
    tex.materialRect1W = batch.materialRect1W;
    tex.materialRect1H = batch.materialRect1H;
    tex.materialFlipbook0Cols = batch.materialFlipbook0Cols;
    tex.materialFlipbook0Rows = batch.materialFlipbook0Rows;
    tex.materialFlipbook0Frames = batch.materialFlipbook0Frames;
    tex.materialFlipbook0Fps = batch.materialFlipbook0Fps;
    tex.materialFlipbook1Cols = batch.materialFlipbook1Cols;
    tex.materialFlipbook1Rows = batch.materialFlipbook1Rows;
    tex.materialFlipbook1Frames = batch.materialFlipbook1Frames;
    tex.materialFlipbook1Fps = batch.materialFlipbook1Fps;
    tex.modelMatrix = batch.modelMatrix;
    tex.gpuSkinning = batch.gpuSkinning;
    tex.skinMatrixCount = batch.skinMatrixCount;
    tex.skinMatrices = batch.sharedSkinMatrices
        ? batch.sharedSkinMatrices
        : (batch.skinMatrices.empty() ? nullptr : batch.skinMatrices.data());
    return tex;
}

void drawOneBatch(IRenderBackend& renderer,
                  const WorldIndexedBatch& batch,
                  const float* viewProjectionMatrix4x4,
                  int surfaceWidth,
                  int surfaceHeight,
                  const float* cameraWorldPos3,
                  const float* cameraForward3,
                  const float* cameraTarget3) {
    const IRenderBackend::WorldMeshVertex* vertices = batch.sharedVertices
        ? batch.sharedVertices
        : batch.vertices.data();
    const std::size_t vertexCount = batch.sharedVertices
        ? batch.sharedVertexCount
        : batch.vertices.size();
    const std::uint32_t* indices = batch.sharedIndices
        ? batch.sharedIndices
        : batch.indices.data();
    const std::size_t indexCount = batch.sharedIndices
        ? batch.sharedIndexCount
        : batch.indices.size();
    if (!vertices || !indices || vertexCount == 0u || indexCount == 0u) return;

    IRenderBackend::WorldTextureData tex =
        toWorldTextureData(batch, cameraWorldPos3, cameraForward3, cameraTarget3);
    renderer.drawWorldIndexedMeshTextured(
        vertices,
        vertexCount,
        indices,
        indexCount,
        &tex,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
}

} // namespace

void submitWorldIndexedBatches(IRenderBackend& renderer,
                               const std::vector<WorldIndexedBatch>& batches,
                               const float* viewProjectionMatrix4x4,
                               int surfaceWidth,
                               int surfaceHeight,
                               const float* cameraWorldPos3,
                               const float* cameraForward3,
                               const float* cameraTarget3) {
    if (batches.empty() || !viewProjectionMatrix4x4 || surfaceWidth <= 0 || surfaceHeight <= 0) return;

    for (const WorldIndexedBatch& batch : batches) {
        if (!batch.hasGeometry()) continue;
        if (batch.alphaMode == 2u) continue;
        drawOneBatch(
            renderer,
            batch,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight,
            cameraWorldPos3,
            cameraForward3,
            cameraTarget3);
    }

    static thread_local std::vector<const WorldIndexedBatch*> blendBatches;
    blendBatches.clear();
    if (blendBatches.capacity() < batches.size()) {
        blendBatches.reserve(batches.size());
    }
    for (const WorldIndexedBatch& batch : batches) {
        if (!batch.hasGeometry()) continue;
        if (batch.alphaMode != 2u) continue;
        blendBatches.push_back(&batch);
    }
    std::stable_sort(
        blendBatches.begin(),
        blendBatches.end(),
        [](const WorldIndexedBatch* lhs, const WorldIndexedBatch* rhs) {
            return lhs->sortDepth > rhs->sortDepth;
        });
    for (const WorldIndexedBatch* batch : blendBatches) {
        if (!batch) continue;
        drawOneBatch(
            renderer,
            *batch,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight,
            cameraWorldPos3,
            cameraForward3,
            cameraTarget3);
    }
}

} // namespace game::runtime::shared_world_batches

