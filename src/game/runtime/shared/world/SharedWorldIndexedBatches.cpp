#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <algorithm>
#include <cstring>
#include <string_view>
#include <unordered_map>

namespace game::runtime::shared_world_batches {

namespace {

struct AutoInstanceKey {
    const WorldIndexedBatch* sharedTemplate = nullptr;
    std::string_view geometryCacheKey{};
    bool materialAlphaOverride = false;
    std::uint8_t alphaMode = 0u;
    std::uint8_t blendMode = 0u;
    float alphaCutoff = 0.0f;

    bool operator==(const AutoInstanceKey& other) const {
        return sharedTemplate == other.sharedTemplate &&
               geometryCacheKey == other.geometryCacheKey &&
               materialAlphaOverride == other.materialAlphaOverride &&
               alphaMode == other.alphaMode &&
               blendMode == other.blendMode &&
               alphaCutoff == other.alphaCutoff;
    }
};

struct AutoInstanceKeyHash {
    std::size_t operator()(const AutoInstanceKey& key) const {
        std::size_t h = std::hash<const WorldIndexedBatch*>{}(key.sharedTemplate);
        h ^= std::hash<std::string_view>{}(key.geometryCacheKey) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(key.materialAlphaOverride) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint8_t>{}(key.alphaMode) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint8_t>{}(key.blendMode) + 0x9e3779b9u + (h << 6) + (h >> 2);
        std::uint32_t alphaBits = 0u;
        static_assert(sizeof(alphaBits) == sizeof(key.alphaCutoff));
        std::memcpy(&alphaBits, &key.alphaCutoff, sizeof(alphaBits));
        h ^= std::hash<std::uint32_t>{}(alphaBits) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

bool hasLocalMaterialPayload(const WorldIndexedBatch& batch) {
    return !batch.textureKey.empty() ||
           !batch.textureCacheKey.empty() ||
           !batch.ownedTextureRgba.empty() ||
           batch.textureRgba != nullptr ||
           batch.textureWidth > 0 ||
           batch.textureHeight > 0 ||
           !batch.normalTextureKey.empty() ||
           !batch.normalTextureCacheKey.empty() ||
           !batch.ownedNormalTextureRgba.empty() ||
           batch.normalTextureRgba != nullptr ||
           batch.normalTextureWidth > 0 ||
           batch.normalTextureHeight > 0 ||
           !batch.metallicRoughnessTextureKey.empty() ||
           !batch.metallicRoughnessTextureCacheKey.empty() ||
           !batch.ownedMetallicRoughnessTextureRgba.empty() ||
           batch.metallicRoughnessTextureRgba != nullptr ||
           batch.metallicRoughnessTextureWidth > 0 ||
           batch.metallicRoughnessTextureHeight > 0 ||
           !batch.occlusionTextureKey.empty() ||
           !batch.occlusionTextureCacheKey.empty() ||
           !batch.ownedOcclusionTextureRgba.empty() ||
           batch.occlusionTextureRgba != nullptr ||
           batch.occlusionTextureWidth > 0 ||
           batch.occlusionTextureHeight > 0 ||
           !batch.emissiveTextureKey.empty() ||
           !batch.emissiveTextureCacheKey.empty() ||
           !batch.ownedEmissiveTextureRgba.empty() ||
           batch.emissiveTextureRgba != nullptr ||
           batch.emissiveTextureWidth > 0 ||
           batch.emissiveTextureHeight > 0;
}

bool canAutoInstance(const IRenderBackend& renderer, const WorldIndexedBatch& batch) {
    if (!renderer.supportsWorldIndexedMeshInstancing()) return false;
    if (!batch.instances.empty()) return false;
    if (batch.sharedTemplate == nullptr) return false;
    if (batch.geometryCacheKey.empty()) return false;
    if (batch.gpuSkinning != 0u) return false;
    if (batch.skinMatrixCount != 0u || batch.sharedSkinMatrices != nullptr || !batch.skinMatrices.empty()) {
        return false;
    }
    if (hasLocalMaterialPayload(batch)) return false;
    return batch.hasGeometry();
}

IRenderBackend::WorldMeshInstance makeWorldMeshInstance(const WorldIndexedBatch& batch) {
    IRenderBackend::WorldMeshInstance instance{};
    instance.modelMatrix = batch.modelMatrix;
    instance.vertexColorMulR = batch.vertexColorMulR;
    instance.vertexColorMulG = batch.vertexColorMulG;
    instance.vertexColorMulB = batch.vertexColorMulB;
    instance.vertexColorMulA = batch.vertexColorMulA;
    return instance;
}

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
    const auto resolveCacheKey = [](const std::string& localKey,
                                    const std::string* templateKey) {
        if (!localKey.empty()) return localKey.c_str();
        if (templateKey && !templateKey->empty()) return templateKey->c_str();
        return static_cast<const char*>(nullptr);
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
    tex.cacheKey = resolveCacheKey(
        batch.textureCacheKey, templateBatch ? &templateBatch->textureCacheKey : nullptr);
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
    tex.normalCacheKey = resolveCacheKey(
        batch.normalTextureCacheKey,
        templateBatch ? &templateBatch->normalTextureCacheKey : nullptr);
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
    tex.metallicRoughnessCacheKey = resolveCacheKey(
        batch.metallicRoughnessTextureCacheKey,
        templateBatch ? &templateBatch->metallicRoughnessTextureCacheKey : nullptr);
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
    tex.occlusionCacheKey = resolveCacheKey(
        batch.occlusionTextureCacheKey,
        templateBatch ? &templateBatch->occlusionTextureCacheKey : nullptr);
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
    tex.emissiveCacheKey = resolveCacheKey(
        batch.emissiveTextureCacheKey,
        templateBatch ? &templateBatch->emissiveTextureCacheKey : nullptr);
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
    if (!batch.instances.empty()) {
        renderer.drawWorldIndexedMeshTexturedCachedInstanced(
            batch.geometryCacheKey.empty() ? nullptr : batch.geometryCacheKey.c_str(),
            vertices,
            vertexCount,
            indices,
            indexCount,
            &tex,
            batch.instances.data(),
            batch.instances.size(),
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
    } else if (!batch.geometryCacheKey.empty()) {
        renderer.drawWorldIndexedMeshTexturedCached(
            batch.geometryCacheKey.c_str(),
            vertices,
            vertexCount,
            indices,
            indexCount,
            &tex,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
    } else {
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

std::size_t prewarmWorldIndexedBatches(IRenderBackend& renderer,
                                       const std::vector<WorldIndexedBatch>& batches,
                                       const float* cameraWorldPos3,
                                       const float* cameraForward3,
                                       const float* cameraTarget3) {
    std::size_t warmed = 0u;
    for (const WorldIndexedBatch& batch : batches) {
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
        if (!vertices || !indices || vertexCount == 0u || indexCount == 0u) continue;

        IRenderBackend::WorldTextureData tex =
            toWorldTextureData(batch, cameraWorldPos3, cameraForward3, cameraTarget3);
        renderer.prewarmWorldTextureData(&tex);
        if (!batch.geometryCacheKey.empty()) {
            renderer.prewarmWorldIndexedMeshCached(
                batch.geometryCacheKey.c_str(),
                vertices,
                vertexCount,
                indices,
                indexCount);
        }
        ++warmed;
    }
    return warmed;
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
    static thread_local std::vector<WorldIndexedBatch> autoInstancedOpaqueBatches;
    static thread_local std::unordered_map<AutoInstanceKey, std::size_t, AutoInstanceKeyHash>
        autoInstanceBatchIndex;
    opaqueBatches.clear();
    blendBatches.clear();
    autoInstancedOpaqueBatches.clear();
    autoInstanceBatchIndex.clear();
    if (opaqueBatches.capacity() < batches.size()) {
        opaqueBatches.reserve(batches.size());
    }
    if (blendBatches.capacity() < batches.size()) {
        blendBatches.reserve(batches.size());
    }
    if (autoInstancedOpaqueBatches.capacity() < batches.size()) {
        autoInstancedOpaqueBatches.reserve(batches.size());
    }
    autoInstanceBatchIndex.reserve(batches.size());

    for (const WorldIndexedBatch& batch : batches) {
        if (!batch.hasGeometry()) continue;
        if (batch.alphaMode == 2u) {
            blendBatches.push_back(&batch);
        } else if (canAutoInstance(renderer, batch)) {
            AutoInstanceKey key{};
            key.sharedTemplate = batch.sharedTemplate;
            key.geometryCacheKey = batch.geometryCacheKey;
            key.materialAlphaOverride = batch.materialAlphaOverride;
            key.alphaMode = batch.alphaMode;
            key.blendMode = batch.blendMode;
            key.alphaCutoff = batch.alphaCutoff;
            auto it = autoInstanceBatchIndex.find(key);
            if (it == autoInstanceBatchIndex.end()) {
                autoInstancedOpaqueBatches.push_back(batch);
                WorldIndexedBatch& instancedBatch = autoInstancedOpaqueBatches.back();
                instancedBatch.instances.clear();
                instancedBatch.instances.push_back(makeWorldMeshInstance(batch));
                instancedBatch.vertexColorMulR = 1.0f;
                instancedBatch.vertexColorMulG = 1.0f;
                instancedBatch.vertexColorMulB = 1.0f;
                instancedBatch.vertexColorMulA = 1.0f;
                instancedBatch.modelMatrix = {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f};
                const std::size_t index = autoInstancedOpaqueBatches.size() - 1u;
                autoInstanceBatchIndex.emplace(key, index);
                opaqueBatches.push_back(&autoInstancedOpaqueBatches.back());
            } else {
                autoInstancedOpaqueBatches[it->second].instances.push_back(
                    makeWorldMeshInstance(batch));
            }
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

