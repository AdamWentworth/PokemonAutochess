#include "game/runtime/shared/scene/SharedWorldScene.h"

#include <algorithm>
#include <string_view>

namespace game::runtime::shared_world_scene {

namespace {

struct RenderObjectKey {
    std::uint32_t geometryId = 0u;
    std::uint32_t materialId = 0u;
    std::uint8_t pipelineVariant = 0u;
    std::uint32_t cookedDrawSlot = 0u;
    bool skinned = false;

    bool operator==(const RenderObjectKey& other) const {
        return geometryId == other.geometryId &&
               materialId == other.materialId &&
               pipelineVariant == other.pipelineVariant &&
               cookedDrawSlot == other.cookedDrawSlot &&
               skinned == other.skinned;
    }
};

struct RenderObjectKeyHash {
    std::size_t operator()(const RenderObjectKey& key) const noexcept {
        std::size_t h = static_cast<std::size_t>(key.geometryId);
        h ^= static_cast<std::size_t>(key.materialId) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(key.pipelineVariant) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(key.cookedDrawSlot) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(key.skinned ? 1u : 0u) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

auto& renderObjectByKey() {
    static thread_local std::unordered_map<
        const WorldSceneRegistry*,
        std::unordered_map<RenderObjectKey,
                           IRenderBackend::WorldSceneRenderObjectHandle,
                           RenderObjectKeyHash>>
        caches;
    return caches;
}

const shared_world_batches::WorldIndexedBatch& batchTemplateOrSelf(
    const shared_world_batches::WorldIndexedBatch& batch) {
    return batch.sharedTemplate ? *batch.sharedTemplate : batch;
}

std::string_view resolvedStringMember(
    const shared_world_batches::WorldIndexedBatch& batch,
    const std::string shared_world_batches::WorldIndexedBatch::*member) {
    if (!(batch.*member).empty()) return batch.*member;
    if (batch.sharedTemplate && !((batch.sharedTemplate->*member).empty())) {
        return batch.sharedTemplate->*member;
    }
    return {};
}

template <typename T>
void assignIfPresent(const shared_world_batches::WorldIndexedBatch& batch,
                     T& outValue,
                     T shared_world_batches::WorldIndexedBatch::*member) {
    if (batch.*member) {
        outValue = batch.*member;
    } else if (batch.sharedTemplate) {
        outValue = batch.sharedTemplate->*member;
    }
}

template <typename T>
void assignIfPositive(const shared_world_batches::WorldIndexedBatch& batch,
                      T& outValue,
                      T shared_world_batches::WorldIndexedBatch::*member) {
    if ((batch.*member) > 0) {
        outValue = batch.*member;
    } else if (batch.sharedTemplate) {
        outValue = batch.sharedTemplate->*member;
    }
}

IRenderBackend::WorldSceneMaterial makeMaterialFromBatchTemplate(
    const shared_world_batches::WorldIndexedBatch& batchTemplate) {
    const auto& batch = batchTemplateOrSelf(batchTemplate);

    IRenderBackend::WorldSceneMaterial material{};
    material.textureKey = std::string(
        resolvedStringMember(batchTemplate, &shared_world_batches::WorldIndexedBatch::textureKey));
    material.textureCacheKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::textureCacheKey));
    assignIfPresent(
        batchTemplate,
        material.textureRgba,
        &shared_world_batches::WorldIndexedBatch::textureRgba);
    assignIfPositive(
        batchTemplate,
        material.textureWidth,
        &shared_world_batches::WorldIndexedBatch::textureWidth);
    assignIfPositive(
        batchTemplate,
        material.textureHeight,
        &shared_world_batches::WorldIndexedBatch::textureHeight);
    material.textureWrapS = batch.textureWrapS;
    material.textureWrapT = batch.textureWrapT;

    material.normalTextureKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::normalTextureKey));
    material.normalTextureCacheKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::normalTextureCacheKey));
    assignIfPresent(
        batchTemplate,
        material.normalTextureRgba,
        &shared_world_batches::WorldIndexedBatch::normalTextureRgba);
    assignIfPositive(
        batchTemplate,
        material.normalTextureWidth,
        &shared_world_batches::WorldIndexedBatch::normalTextureWidth);
    assignIfPositive(
        batchTemplate,
        material.normalTextureHeight,
        &shared_world_batches::WorldIndexedBatch::normalTextureHeight);
    material.normalTextureWrapS = batch.normalTextureWrapS;
    material.normalTextureWrapT = batch.normalTextureWrapT;

    material.metallicRoughnessTextureKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::metallicRoughnessTextureKey));
    material.metallicRoughnessTextureCacheKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::metallicRoughnessTextureCacheKey));
    assignIfPresent(
        batchTemplate,
        material.metallicRoughnessTextureRgba,
        &shared_world_batches::WorldIndexedBatch::metallicRoughnessTextureRgba);
    assignIfPositive(
        batchTemplate,
        material.metallicRoughnessTextureWidth,
        &shared_world_batches::WorldIndexedBatch::metallicRoughnessTextureWidth);
    assignIfPositive(
        batchTemplate,
        material.metallicRoughnessTextureHeight,
        &shared_world_batches::WorldIndexedBatch::metallicRoughnessTextureHeight);
    material.metallicRoughnessTextureWrapS = batch.metallicRoughnessTextureWrapS;
    material.metallicRoughnessTextureWrapT = batch.metallicRoughnessTextureWrapT;

    material.occlusionTextureKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::occlusionTextureKey));
    material.occlusionTextureCacheKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::occlusionTextureCacheKey));
    assignIfPresent(
        batchTemplate,
        material.occlusionTextureRgba,
        &shared_world_batches::WorldIndexedBatch::occlusionTextureRgba);
    assignIfPositive(
        batchTemplate,
        material.occlusionTextureWidth,
        &shared_world_batches::WorldIndexedBatch::occlusionTextureWidth);
    assignIfPositive(
        batchTemplate,
        material.occlusionTextureHeight,
        &shared_world_batches::WorldIndexedBatch::occlusionTextureHeight);
    material.occlusionTextureWrapS = batch.occlusionTextureWrapS;
    material.occlusionTextureWrapT = batch.occlusionTextureWrapT;

    material.emissiveTextureKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::emissiveTextureKey));
    material.emissiveTextureCacheKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::emissiveTextureCacheKey));
    assignIfPresent(
        batchTemplate,
        material.emissiveTextureRgba,
        &shared_world_batches::WorldIndexedBatch::emissiveTextureRgba);
    assignIfPositive(
        batchTemplate,
        material.emissiveTextureWidth,
        &shared_world_batches::WorldIndexedBatch::emissiveTextureWidth);
    assignIfPositive(
        batchTemplate,
        material.emissiveTextureHeight,
        &shared_world_batches::WorldIndexedBatch::emissiveTextureHeight);
    material.emissiveTextureWrapS = batch.emissiveTextureWrapS;
    material.emissiveTextureWrapT = batch.emissiveTextureWrapT;

    material.alphaMode = batch.alphaMode;
    material.blendMode = batch.blendMode;
    material.materialMode = batch.materialMode;
    material.alphaCutoff = batch.alphaCutoff;
    material.normalScale = batch.normalScale;
    material.metallicFactor = batch.metallicFactor;
    material.roughnessFactor = batch.roughnessFactor;
    material.occlusionStrength = batch.occlusionStrength;
    material.emissiveFactorR = batch.emissiveFactorR;
    material.emissiveFactorG = batch.emissiveFactorG;
    material.emissiveFactorB = batch.emissiveFactorB;
    material.characterInkingEnabled = batch.characterInkingEnabled;
    material.materialTimeSec = batch.materialTimeSec;
    material.materialFlags = batch.materialFlags;
    material.materialAtlasWidth = batch.materialAtlasWidth;
    material.materialAtlasHeight = batch.materialAtlasHeight;
    material.materialRect0U = batch.materialRect0U;
    material.materialRect0V = batch.materialRect0V;
    material.materialRect0W = batch.materialRect0W;
    material.materialRect0H = batch.materialRect0H;
    material.materialRect1U = batch.materialRect1U;
    material.materialRect1V = batch.materialRect1V;
    material.materialRect1W = batch.materialRect1W;
    material.materialRect1H = batch.materialRect1H;
    material.materialFlipbook0Cols = batch.materialFlipbook0Cols;
    material.materialFlipbook0Rows = batch.materialFlipbook0Rows;
    material.materialFlipbook0Frames = batch.materialFlipbook0Frames;
    material.materialFlipbook0Fps = batch.materialFlipbook0Fps;
    material.materialFlipbook1Cols = batch.materialFlipbook1Cols;
    material.materialFlipbook1Rows = batch.materialFlipbook1Rows;
    material.materialFlipbook1Frames = batch.materialFlipbook1Frames;
    material.materialFlipbook1Fps = batch.materialFlipbook1Fps;
    return material;
}

IRenderBackend::WorldSceneDrawClass& findOrCreateDrawClass(
    IRenderBackend::WorldSceneFrame& frame,
    IRenderBackend::WorldSceneRenderObjectHandle objectHandle) {
    if (objectHandle.id != 0u) {
        if (frame.drawClassIndexByObjectId.size() < objectHandle.id) {
            frame.drawClassIndexByObjectId.resize(objectHandle.id, 0u);
        }
        std::uint32_t& cachedIndex =
            frame.drawClassIndexByObjectId[static_cast<std::size_t>(objectHandle.id - 1u)];
        if (cachedIndex != 0u) {
            return frame.drawClasses[static_cast<std::size_t>(cachedIndex - 1u)];
        }

        frame.drawClasses.push_back(IRenderBackend::WorldSceneDrawClass{});
        auto& drawClass = frame.drawClasses.back();
        drawClass.objectHandle = objectHandle;
        cachedIndex = static_cast<std::uint32_t>(frame.drawClasses.size());
        return drawClass;
    }

    frame.drawClasses.push_back(IRenderBackend::WorldSceneDrawClass{});
    auto& drawClass = frame.drawClasses.back();
    drawClass.objectHandle = objectHandle;
    return drawClass;
}

} // namespace

void resetWorldSceneRegistry(WorldSceneRegistry& registry) {
    registry.geometries.clear();
    registry.materials.clear();
    registry.skeletonLayouts.clear();
    registry.animationClips.clear();
    registry.renderObjects.clear();
    registry.geometryByIdentity.clear();
    registry.materialByIdentity.clear();
    ++registry.generation;
    if (registry.generation == 0u) {
        registry.generation = 1u;
    }
    renderObjectByKey().erase(&registry);
}

void beginWorldSceneFrame(IRenderBackend::WorldSceneFrame& frame) {
    frame.clear();
}

IRenderBackend::WorldSceneGeometryHandle ensureRigidGeometry(
    WorldSceneRegistry& registry,
    const void* identity,
    const char* geometryCacheKey,
    const IRenderBackend::WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount) {
    if (identity) {
        const auto found = registry.geometryByIdentity.find(identity);
        if (found != registry.geometryByIdentity.end()) {
            return found->second;
        }
    }

    IRenderBackend::WorldSceneGeometryHandle handle{};
    handle.id = static_cast<std::uint32_t>(registry.geometries.size() + 1u);
    IRenderBackend::WorldSceneGeometry geometry{};
    geometry.handle = handle;
    geometry.geometryCacheKey = geometryCacheKey ? geometryCacheKey : "";
    geometry.vertices = vertices;
    geometry.vertexCount = vertexCount;
    geometry.indices = indices;
    geometry.indexCount = indexCount;
    registry.geometries.push_back(std::move(geometry));
    if (identity) {
        registry.geometryByIdentity.emplace(identity, handle);
    }
    return handle;
}

IRenderBackend::WorldSceneMaterialHandle ensureMaterialFromBatchTemplate(
    WorldSceneRegistry& registry,
    const void* identity,
    const shared_world_batches::WorldIndexedBatch& batchTemplate) {
    return ensureMaterial(registry, identity, makeMaterialFromBatchTemplate(batchTemplate));
}

IRenderBackend::WorldSceneMaterialHandle ensureMaterial(
    WorldSceneRegistry& registry,
    const void* identity,
    const IRenderBackend::WorldSceneMaterial& materialTemplate) {
    if (identity) {
        const auto found = registry.materialByIdentity.find(identity);
        if (found != registry.materialByIdentity.end()) {
            return found->second;
        }
    }

    IRenderBackend::WorldSceneMaterialHandle handle{};
    handle.id = static_cast<std::uint32_t>(registry.materials.size() + 1u);
    IRenderBackend::WorldSceneMaterial material = materialTemplate;
    material.handle = handle;

    registry.materials.push_back(std::move(material));
    if (identity) {
        registry.materialByIdentity.emplace(identity, handle);
    }
    return handle;
}

IRenderBackend::WorldSceneRenderObjectHandle ensureRenderObject(
    WorldSceneRegistry& registry,
    IRenderBackend::WorldSceneGeometryHandle geometryHandle,
    IRenderBackend::WorldSceneMaterialHandle materialHandle,
    PipelineVariant pipelineVariant,
    std::uint32_t cookedDrawSlot,
    bool skinned) {
    auto& cache = renderObjectByKey()[&registry];
    const RenderObjectKey key{
        geometryHandle.id,
        materialHandle.id,
        static_cast<std::uint8_t>(pipelineVariant),
        cookedDrawSlot,
        skinned};
    const auto found = cache.find(key);
    if (found != cache.end()) {
        return found->second;
    }

    IRenderBackend::WorldSceneRenderObjectHandle handle{};
    handle.id = static_cast<std::uint32_t>(registry.renderObjects.size() + 1u);
    IRenderBackend::WorldSceneRenderObject object{};
    object.handle = handle;
    object.geometryHandle = geometryHandle;
    object.materialHandle = materialHandle;
    object.pipelineVariant = static_cast<std::uint8_t>(pipelineVariant);
    object.cookedDrawSlot = cookedDrawSlot;
    object.opaque = true;
    object.skinned = skinned;
    registry.renderObjects.push_back(std::move(object));
    cache.emplace(key, handle);
    return handle;
}

void appendRigidInstance(IRenderBackend::WorldSceneFrame& frame,
                         IRenderBackend::WorldSceneRenderObjectHandle objectHandle,
                         IRenderBackend::WorldSceneRenderInstanceHandle instanceHandle,
                         const std::array<float, 16>& modelMatrix,
                         float vertexColorMulR,
                         float vertexColorMulG,
                         float vertexColorMulB,
                         float vertexColorMulA,
                         float sortDepth) {
    auto& drawClass = findOrCreateDrawClass(frame, objectHandle);

    IRenderBackend::WorldSceneInstance instance{};
    instance.handle = instanceHandle;
    instance.objectHandle = objectHandle;
    instance.modelMatrix = modelMatrix;
    instance.vertexColorMulR = vertexColorMulR;
    instance.vertexColorMulG = vertexColorMulG;
    instance.vertexColorMulB = vertexColorMulB;
    instance.vertexColorMulA = vertexColorMulA;
    instance.sortDepth = sortDepth;
    drawClass.instances.push_back(std::move(instance));
}

void appendSkinnedInstance(IRenderBackend::WorldSceneFrame& frame,
                           IRenderBackend::WorldSceneRenderObjectHandle objectHandle,
                           IRenderBackend::WorldSceneRenderInstanceHandle instanceHandle,
                           const std::array<float, 16>& modelMatrix,
                           float vertexColorMulR,
                           float vertexColorMulG,
                           float vertexColorMulB,
                           float vertexColorMulA,
                           float sortDepth,
                           std::uint8_t gpuSkinningMode,
                           std::uint32_t skinMatrixCount,
                           const float* skinMatrices) {
    if (!skinMatrices || skinMatrixCount == 0u) {
        appendRigidInstance(frame,
                            objectHandle,
                            instanceHandle,
                            modelMatrix,
                            vertexColorMulR,
                            vertexColorMulG,
                            vertexColorMulB,
                            vertexColorMulA,
                            sortDepth);
        return;
    }

    auto& drawClass = findOrCreateDrawClass(frame, objectHandle);

    IRenderBackend::WorldSceneInstance instance{};
    instance.handle = instanceHandle;
    instance.objectHandle = objectHandle;
    instance.modelMatrix = modelMatrix;
    instance.vertexColorMulR = vertexColorMulR;
    instance.vertexColorMulG = vertexColorMulG;
    instance.vertexColorMulB = vertexColorMulB;
    instance.vertexColorMulA = vertexColorMulA;
    instance.gpuSkinning = 1u;
    instance.gpuSkinningMode = std::min<std::uint8_t>(gpuSkinningMode, 1u);
    instance.skinMatrixCount = skinMatrixCount;
    instance.skinMatrices = skinMatrices;
    instance.sortDepth = sortDepth;
    drawClass.instances.push_back(std::move(instance));

    const std::uint32_t floatsPerSkinMatrix =
        instance.gpuSkinningMode == 1u ? 32u : 16u;
    const std::uint32_t uploadBytes =
        skinMatrixCount * floatsPerSkinMatrix * static_cast<std::uint32_t>(sizeof(float));
    ++drawClass.visibleSkeletons;
    drawClass.paletteUploadBytes += uploadBytes;
    ++frame.visibleSkeletons;
    frame.paletteUploadBytes += uploadBytes;
}

IRenderBackend::WorldSceneView buildWorldSceneView(
    const WorldSceneRegistry& registry,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight,
    const float* cameraWorldPos3,
    const float* cameraForward3,
    const float* cameraTarget3) {
    IRenderBackend::WorldSceneView out{};
    out.geometries = &registry.geometries;
    out.materials = &registry.materials;
    out.skeletonLayouts = &registry.skeletonLayouts;
    out.animationClips = &registry.animationClips;
    out.renderObjects = &registry.renderObjects;
    out.registryGeneration = registry.generation;
    out.viewProjectionMatrix4x4 = viewProjectionMatrix4x4;
    out.surfaceWidth = surfaceWidth;
    out.surfaceHeight = surfaceHeight;
    if (cameraWorldPos3) {
        out.cameraWorldPos[0] = cameraWorldPos3[0];
        out.cameraWorldPos[1] = cameraWorldPos3[1];
        out.cameraWorldPos[2] = cameraWorldPos3[2];
    }
    if (cameraForward3) {
        out.cameraForward[0] = cameraForward3[0];
        out.cameraForward[1] = cameraForward3[1];
        out.cameraForward[2] = cameraForward3[2];
    }
    if (cameraTarget3) {
        out.cameraTarget[0] = cameraTarget3[0];
        out.cameraTarget[1] = cameraTarget3[1];
        out.cameraTarget[2] = cameraTarget3[2];
    }
    return out;
}

IRenderBackend::WorldTextureData makeWorldSceneTextureData(
    const IRenderBackend::WorldSceneMaterial& material,
    const float* cameraWorldPos3,
    const float* cameraForward3,
    const float* cameraTarget3) {
    IRenderBackend::WorldTextureData tex{};
    tex.key = material.textureKey.empty() ? "" : material.textureKey.c_str();
    tex.cacheKey =
        material.textureCacheKey.empty() ? nullptr : material.textureCacheKey.c_str();
    tex.rgba = material.textureRgba;
    tex.width = material.textureWidth;
    tex.height = material.textureHeight;
    tex.wrapS = material.textureWrapS;
    tex.wrapT = material.textureWrapT;
    tex.normalKey =
        material.normalTextureKey.empty() ? "" : material.normalTextureKey.c_str();
    tex.normalCacheKey = material.normalTextureCacheKey.empty()
        ? nullptr
        : material.normalTextureCacheKey.c_str();
    tex.normalRgba = material.normalTextureRgba;
    tex.normalWidth = material.normalTextureWidth;
    tex.normalHeight = material.normalTextureHeight;
    tex.normalWrapS = material.normalTextureWrapS;
    tex.normalWrapT = material.normalTextureWrapT;
    tex.metallicRoughnessKey = material.metallicRoughnessTextureKey.empty()
        ? ""
        : material.metallicRoughnessTextureKey.c_str();
    tex.metallicRoughnessCacheKey = material.metallicRoughnessTextureCacheKey.empty()
        ? nullptr
        : material.metallicRoughnessTextureCacheKey.c_str();
    tex.metallicRoughnessRgba = material.metallicRoughnessTextureRgba;
    tex.metallicRoughnessWidth = material.metallicRoughnessTextureWidth;
    tex.metallicRoughnessHeight = material.metallicRoughnessTextureHeight;
    tex.metallicRoughnessWrapS = material.metallicRoughnessTextureWrapS;
    tex.metallicRoughnessWrapT = material.metallicRoughnessTextureWrapT;
    tex.occlusionKey =
        material.occlusionTextureKey.empty() ? "" : material.occlusionTextureKey.c_str();
    tex.occlusionCacheKey = material.occlusionTextureCacheKey.empty()
        ? nullptr
        : material.occlusionTextureCacheKey.c_str();
    tex.occlusionRgba = material.occlusionTextureRgba;
    tex.occlusionWidth = material.occlusionTextureWidth;
    tex.occlusionHeight = material.occlusionTextureHeight;
    tex.occlusionWrapS = material.occlusionTextureWrapS;
    tex.occlusionWrapT = material.occlusionTextureWrapT;
    tex.emissiveKey =
        material.emissiveTextureKey.empty() ? "" : material.emissiveTextureKey.c_str();
    tex.emissiveCacheKey = material.emissiveTextureCacheKey.empty()
        ? nullptr
        : material.emissiveTextureCacheKey.c_str();
    tex.emissiveRgba = material.emissiveTextureRgba;
    tex.emissiveWidth = material.emissiveTextureWidth;
    tex.emissiveHeight = material.emissiveTextureHeight;
    tex.emissiveWrapS = material.emissiveTextureWrapS;
    tex.emissiveWrapT = material.emissiveTextureWrapT;
    tex.alphaMode = material.alphaMode;
    tex.blendMode = material.blendMode;
    tex.materialMode = material.materialMode;
    tex.alphaCutoff = material.alphaCutoff;
    tex.normalScale = material.normalScale;
    tex.metallicFactor = material.metallicFactor;
    tex.roughnessFactor = material.roughnessFactor;
    tex.occlusionStrength = material.occlusionStrength;
    tex.emissiveFactorR = material.emissiveFactorR;
    tex.emissiveFactorG = material.emissiveFactorG;
    tex.emissiveFactorB = material.emissiveFactorB;
    tex.characterInkingEnabled = material.characterInkingEnabled;
    tex.materialTimeSec = material.materialTimeSec;
    tex.materialFlags = material.materialFlags;
    tex.materialAtlasWidth = material.materialAtlasWidth;
    tex.materialAtlasHeight = material.materialAtlasHeight;
    tex.materialRect0U = material.materialRect0U;
    tex.materialRect0V = material.materialRect0V;
    tex.materialRect0W = material.materialRect0W;
    tex.materialRect0H = material.materialRect0H;
    tex.materialRect1U = material.materialRect1U;
    tex.materialRect1V = material.materialRect1V;
    tex.materialRect1W = material.materialRect1W;
    tex.materialRect1H = material.materialRect1H;
    tex.materialFlipbook0Cols = material.materialFlipbook0Cols;
    tex.materialFlipbook0Rows = material.materialFlipbook0Rows;
    tex.materialFlipbook0Frames = material.materialFlipbook0Frames;
    tex.materialFlipbook0Fps = material.materialFlipbook0Fps;
    tex.materialFlipbook1Cols = material.materialFlipbook1Cols;
    tex.materialFlipbook1Rows = material.materialFlipbook1Rows;
    tex.materialFlipbook1Frames = material.materialFlipbook1Frames;
    tex.materialFlipbook1Fps = material.materialFlipbook1Fps;
    if (cameraWorldPos3) {
        tex.cameraPosX = cameraWorldPos3[0];
        tex.cameraPosY = cameraWorldPos3[1];
        tex.cameraPosZ = cameraWorldPos3[2];
    }
    if (cameraForward3) {
        tex.cameraForwardX = cameraForward3[0];
        tex.cameraForwardY = cameraForward3[1];
        tex.cameraForwardZ = cameraForward3[2];
    }
    if (cameraTarget3) {
        tex.cameraTargetX = cameraTarget3[0];
        tex.cameraTargetY = cameraTarget3[1];
        tex.cameraTargetZ = cameraTarget3[2];
    }
    return tex;
}

} // namespace game::runtime::shared_world_scene
