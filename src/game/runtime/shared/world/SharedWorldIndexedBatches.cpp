#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <algorithm>

namespace game::runtime::shared_world_batches {

namespace {

const WorldIndexedBatch& materialTemplateOrSelf(const WorldIndexedBatch& batch) {
    return batch.sharedTemplate ? *batch.sharedTemplate : batch;
}

IRenderBackend::WorldTextureData toWorldTextureData(const WorldIndexedBatch& batch,
                                                    const float* cameraWorldPos3,
                                                    const float* cameraForward3,
                                                    const float* cameraTarget3) {
    const WorldIndexedBatch* templateBatch = batch.sharedTemplate;
    const WorldIndexedBatch& materialBatch = materialTemplateOrSelf(batch);
    const auto resolveKey = [](const std::string& localKey, const std::string* templateKey) {
        if (!localKey.empty()) return localKey.c_str();
        if (templateKey && !templateKey->empty()) return templateKey->c_str();
        return "";
    };
    const auto resolveRgba = [](const unsigned char* localPtr,
                                const std::vector<unsigned char>& localOwned,
                                const unsigned char* templatePtr,
                                const std::vector<unsigned char>* templateOwned) {
        if (localPtr) return localPtr;
        if (!localOwned.empty()) return localOwned.data();
        if (templatePtr) return templatePtr;
        if (templateOwned && !templateOwned->empty()) return templateOwned->data();
        return static_cast<const unsigned char*>(nullptr);
    };

    const unsigned char* rgbaData = resolveRgba(
        batch.textureRgba,
        batch.ownedTextureRgba,
        templateBatch ? templateBatch->textureRgba : nullptr,
        templateBatch ? &templateBatch->ownedTextureRgba : nullptr);
    const unsigned char* normalRgbaData = resolveRgba(
        batch.normalTextureRgba,
        batch.ownedNormalTextureRgba,
        templateBatch ? templateBatch->normalTextureRgba : nullptr,
        templateBatch ? &templateBatch->ownedNormalTextureRgba : nullptr);
    const unsigned char* mrRgbaData = resolveRgba(
        batch.metallicRoughnessTextureRgba,
        batch.ownedMetallicRoughnessTextureRgba,
        templateBatch ? templateBatch->metallicRoughnessTextureRgba : nullptr,
        templateBatch ? &templateBatch->ownedMetallicRoughnessTextureRgba : nullptr);
    const unsigned char* occlusionRgbaData = resolveRgba(
        batch.occlusionTextureRgba,
        batch.ownedOcclusionTextureRgba,
        templateBatch ? templateBatch->occlusionTextureRgba : nullptr,
        templateBatch ? &templateBatch->ownedOcclusionTextureRgba : nullptr);
    const unsigned char* emissiveRgbaData = resolveRgba(
        batch.emissiveTextureRgba,
        batch.ownedEmissiveTextureRgba,
        templateBatch ? templateBatch->emissiveTextureRgba : nullptr,
        templateBatch ? &templateBatch->ownedEmissiveTextureRgba : nullptr);

    IRenderBackend::WorldTextureData tex;
    tex.key = resolveKey(
        batch.textureKey, templateBatch ? &templateBatch->textureKey : nullptr);
    tex.rgba = rgbaData;
    tex.width = batch.textureWidth > 0
        ? batch.textureWidth
        : (templateBatch ? templateBatch->textureWidth : batch.textureWidth);
    tex.height = batch.textureHeight > 0
        ? batch.textureHeight
        : (templateBatch ? templateBatch->textureHeight : batch.textureHeight);
    tex.wrapS = (batch.textureWidth > 0 && batch.textureHeight > 0)
        ? batch.textureWrapS
        : (templateBatch ? templateBatch->textureWrapS : batch.textureWrapS);
    tex.wrapT = (batch.textureWidth > 0 && batch.textureHeight > 0)
        ? batch.textureWrapT
        : (templateBatch ? templateBatch->textureWrapT : batch.textureWrapT);
    tex.normalKey = resolveKey(
        batch.normalTextureKey, templateBatch ? &templateBatch->normalTextureKey : nullptr);
    tex.normalRgba = normalRgbaData;
    tex.normalWidth = batch.normalTextureWidth > 0
        ? batch.normalTextureWidth
        : (templateBatch ? templateBatch->normalTextureWidth : batch.normalTextureWidth);
    tex.normalHeight = batch.normalTextureHeight > 0
        ? batch.normalTextureHeight
        : (templateBatch ? templateBatch->normalTextureHeight : batch.normalTextureHeight);
    tex.normalWrapS = (batch.normalTextureWidth > 0 && batch.normalTextureHeight > 0)
        ? batch.normalTextureWrapS
        : (templateBatch ? templateBatch->normalTextureWrapS : batch.normalTextureWrapS);
    tex.normalWrapT = (batch.normalTextureWidth > 0 && batch.normalTextureHeight > 0)
        ? batch.normalTextureWrapT
        : (templateBatch ? templateBatch->normalTextureWrapT : batch.normalTextureWrapT);
    tex.metallicRoughnessKey = resolveKey(
        batch.metallicRoughnessTextureKey,
        templateBatch ? &templateBatch->metallicRoughnessTextureKey : nullptr);
    tex.metallicRoughnessRgba = mrRgbaData;
    tex.metallicRoughnessWidth = batch.metallicRoughnessTextureWidth > 0
        ? batch.metallicRoughnessTextureWidth
        : (templateBatch ? templateBatch->metallicRoughnessTextureWidth
                         : batch.metallicRoughnessTextureWidth);
    tex.metallicRoughnessHeight = batch.metallicRoughnessTextureHeight > 0
        ? batch.metallicRoughnessTextureHeight
        : (templateBatch ? templateBatch->metallicRoughnessTextureHeight
                         : batch.metallicRoughnessTextureHeight);
    tex.metallicRoughnessWrapS =
        (batch.metallicRoughnessTextureWidth > 0 && batch.metallicRoughnessTextureHeight > 0)
        ? batch.metallicRoughnessTextureWrapS
        : (templateBatch ? templateBatch->metallicRoughnessTextureWrapS
                         : batch.metallicRoughnessTextureWrapS);
    tex.metallicRoughnessWrapT =
        (batch.metallicRoughnessTextureWidth > 0 && batch.metallicRoughnessTextureHeight > 0)
        ? batch.metallicRoughnessTextureWrapT
        : (templateBatch ? templateBatch->metallicRoughnessTextureWrapT
                         : batch.metallicRoughnessTextureWrapT);
    tex.occlusionKey = resolveKey(
        batch.occlusionTextureKey, templateBatch ? &templateBatch->occlusionTextureKey : nullptr);
    tex.occlusionRgba = occlusionRgbaData;
    tex.occlusionWidth = batch.occlusionTextureWidth > 0
        ? batch.occlusionTextureWidth
        : (templateBatch ? templateBatch->occlusionTextureWidth : batch.occlusionTextureWidth);
    tex.occlusionHeight = batch.occlusionTextureHeight > 0
        ? batch.occlusionTextureHeight
        : (templateBatch ? templateBatch->occlusionTextureHeight : batch.occlusionTextureHeight);
    tex.occlusionWrapS = (batch.occlusionTextureWidth > 0 && batch.occlusionTextureHeight > 0)
        ? batch.occlusionTextureWrapS
        : (templateBatch ? templateBatch->occlusionTextureWrapS : batch.occlusionTextureWrapS);
    tex.occlusionWrapT = (batch.occlusionTextureWidth > 0 && batch.occlusionTextureHeight > 0)
        ? batch.occlusionTextureWrapT
        : (templateBatch ? templateBatch->occlusionTextureWrapT : batch.occlusionTextureWrapT);
    tex.emissiveKey = resolveKey(
        batch.emissiveTextureKey, templateBatch ? &templateBatch->emissiveTextureKey : nullptr);
    tex.emissiveRgba = emissiveRgbaData;
    tex.emissiveWidth = batch.emissiveTextureWidth > 0
        ? batch.emissiveTextureWidth
        : (templateBatch ? templateBatch->emissiveTextureWidth : batch.emissiveTextureWidth);
    tex.emissiveHeight = batch.emissiveTextureHeight > 0
        ? batch.emissiveTextureHeight
        : (templateBatch ? templateBatch->emissiveTextureHeight : batch.emissiveTextureHeight);
    tex.emissiveWrapS = (batch.emissiveTextureWidth > 0 && batch.emissiveTextureHeight > 0)
        ? batch.emissiveTextureWrapS
        : (templateBatch ? templateBatch->emissiveTextureWrapS : batch.emissiveTextureWrapS);
    tex.emissiveWrapT = (batch.emissiveTextureWidth > 0 && batch.emissiveTextureHeight > 0)
        ? batch.emissiveTextureWrapT
        : (templateBatch ? templateBatch->emissiveTextureWrapT : batch.emissiveTextureWrapT);
    tex.alphaMode = batch.materialAlphaOverride ? batch.alphaMode : materialBatch.alphaMode;
    tex.blendMode = batch.materialAlphaOverride ? batch.blendMode : materialBatch.blendMode;
    tex.materialMode = materialBatch.materialMode;
    tex.alphaCutoff = batch.materialAlphaOverride ? batch.alphaCutoff : materialBatch.alphaCutoff;
    tex.normalScale = materialBatch.normalScale;
    tex.metallicFactor = materialBatch.metallicFactor;
    tex.roughnessFactor = materialBatch.roughnessFactor;
    tex.occlusionStrength = materialBatch.occlusionStrength;
    tex.emissiveFactorR = materialBatch.emissiveFactorR;
    tex.emissiveFactorG = materialBatch.emissiveFactorG;
    tex.emissiveFactorB = materialBatch.emissiveFactorB;
    tex.vertexColorMulR = batch.vertexColorMulR;
    tex.vertexColorMulG = batch.vertexColorMulG;
    tex.vertexColorMulB = batch.vertexColorMulB;
    tex.vertexColorMulA = batch.vertexColorMulA;
    tex.characterInkingEnabled = materialBatch.characterInkingEnabled;
    tex.cameraPosX = (cameraWorldPos3 ? cameraWorldPos3[0] : tex.cameraPosX);
    tex.cameraPosY = (cameraWorldPos3 ? cameraWorldPos3[1] : tex.cameraPosY);
    tex.cameraPosZ = (cameraWorldPos3 ? cameraWorldPos3[2] : tex.cameraPosZ);
    tex.cameraForwardX = (cameraForward3 ? cameraForward3[0] : tex.cameraForwardX);
    tex.cameraForwardY = (cameraForward3 ? cameraForward3[1] : tex.cameraForwardY);
    tex.cameraForwardZ = (cameraForward3 ? cameraForward3[2] : tex.cameraForwardZ);
    tex.cameraTargetX = (cameraTarget3 ? cameraTarget3[0] : tex.cameraTargetX);
    tex.cameraTargetY = (cameraTarget3 ? cameraTarget3[1] : tex.cameraTargetY);
    tex.cameraTargetZ = (cameraTarget3 ? cameraTarget3[2] : tex.cameraTargetZ);
    tex.materialTimeSec = materialBatch.materialTimeSec;
    tex.materialFlags = materialBatch.materialFlags;
    tex.materialAtlasWidth = materialBatch.materialAtlasWidth;
    tex.materialAtlasHeight = materialBatch.materialAtlasHeight;
    tex.materialRect0U = materialBatch.materialRect0U;
    tex.materialRect0V = materialBatch.materialRect0V;
    tex.materialRect0W = materialBatch.materialRect0W;
    tex.materialRect0H = materialBatch.materialRect0H;
    tex.materialRect1U = materialBatch.materialRect1U;
    tex.materialRect1V = materialBatch.materialRect1V;
    tex.materialRect1W = materialBatch.materialRect1W;
    tex.materialRect1H = materialBatch.materialRect1H;
    tex.materialFlipbook0Cols = materialBatch.materialFlipbook0Cols;
    tex.materialFlipbook0Rows = materialBatch.materialFlipbook0Rows;
    tex.materialFlipbook0Frames = materialBatch.materialFlipbook0Frames;
    tex.materialFlipbook0Fps = materialBatch.materialFlipbook0Fps;
    tex.materialFlipbook1Cols = materialBatch.materialFlipbook1Cols;
    tex.materialFlipbook1Rows = materialBatch.materialFlipbook1Rows;
    tex.materialFlipbook1Frames = materialBatch.materialFlipbook1Frames;
    tex.materialFlipbook1Fps = materialBatch.materialFlipbook1Fps;
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
    const bool useSharedVertices = batch.sharedVertices && batch.sharedVertexCount > 0u;
    const bool useSharedIndices = batch.sharedIndices && batch.sharedIndexCount > 0u;
    const IRenderBackend::WorldMeshVertex* vertices = useSharedVertices
        ? batch.sharedVertices
        : batch.vertices.data();
    const std::size_t vertexCount = useSharedVertices
        ? batch.sharedVertexCount
        : batch.vertices.size();
    const std::uint32_t* indices = useSharedIndices
        ? batch.sharedIndices
        : batch.indices.data();
    const std::size_t indexCount = useSharedIndices
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

const WorldIndexedBatch& resolvedMaterialBatch(const WorldIndexedBatch& batch) {
    return materialTemplateOrSelf(batch);
}

bool resolvedHasBaseTexture(const WorldIndexedBatch& batch) {
    const WorldIndexedBatch& materialBatch = materialTemplateOrSelf(batch);
    return materialBatch.textureRgba != nullptr &&
           materialBatch.textureWidth > 0 &&
           materialBatch.textureHeight > 0;
}

bool resolvedHasNormalTexture(const WorldIndexedBatch& batch) {
    const WorldIndexedBatch& materialBatch = materialTemplateOrSelf(batch);
    return materialBatch.normalTextureRgba != nullptr &&
           materialBatch.normalTextureWidth > 0 &&
           materialBatch.normalTextureHeight > 0;
}

void submitWorldIndexedBatches(IRenderBackend& renderer,
                               const std::vector<WorldIndexedBatch>& batches,
                               const float* viewProjectionMatrix4x4,
                               int surfaceWidth,
                               int surfaceHeight,
                               const float* cameraWorldPos3,
                               const float* cameraForward3,
                               const float* cameraTarget3) {
    if (batches.empty() || !viewProjectionMatrix4x4 || surfaceWidth <= 0 || surfaceHeight <= 0) return;

    static thread_local std::vector<const WorldIndexedBatch*> opaqueBatches;
    static thread_local std::vector<const WorldIndexedBatch*> blendBatches;
    opaqueBatches.clear();
    blendBatches.clear();
    if (opaqueBatches.capacity() < batches.size()) {
        opaqueBatches.reserve(batches.size());
    }
    if (blendBatches.capacity() < batches.size()) {
        blendBatches.reserve(batches.size());
    }

    for (const WorldIndexedBatch& batch : batches) {
        if (!batch.hasGeometry()) continue;
        if (batch.alphaMode == 2u) {
            blendBatches.push_back(&batch);
        } else {
            opaqueBatches.push_back(&batch);
        }
    }

    for (const WorldIndexedBatch* batch : opaqueBatches) {
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

    if (blendBatches.size() > 1u) {
        std::stable_sort(
            blendBatches.begin(),
            blendBatches.end(),
            [](const WorldIndexedBatch* lhs, const WorldIndexedBatch* rhs) {
                return lhs->sortDepth > rhs->sortDepth;
            });
    }
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

