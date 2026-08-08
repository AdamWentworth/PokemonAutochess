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
    material.textureMipLevels = batch.textureMipLevels;
    material.textureMipLevelCount = batch.textureMipLevelCount;
    material.textureWrapS = batch.textureWrapS;
    material.textureWrapT = batch.textureWrapT;
    material.textureSrgb = batch.textureSrgb;

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
    material.normalTextureMipLevels = batch.normalTextureMipLevels;
    material.normalTextureMipLevelCount = batch.normalTextureMipLevelCount;
    material.normalTextureWrapS = batch.normalTextureWrapS;
    material.normalTextureWrapT = batch.normalTextureWrapT;
    material.normalTextureSrgb = batch.normalTextureSrgb;

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
    material.metallicRoughnessTextureMipLevels =
        batch.metallicRoughnessTextureMipLevels;
    material.metallicRoughnessTextureMipLevelCount =
        batch.metallicRoughnessTextureMipLevelCount;
    material.metallicRoughnessTextureWrapS = batch.metallicRoughnessTextureWrapS;
    material.metallicRoughnessTextureWrapT = batch.metallicRoughnessTextureWrapT;
    material.metallicRoughnessTextureSrgb =
        batch.metallicRoughnessTextureSrgb;

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
    material.occlusionTextureMipLevels = batch.occlusionTextureMipLevels;
    material.occlusionTextureMipLevelCount = batch.occlusionTextureMipLevelCount;
    material.occlusionTextureWrapS = batch.occlusionTextureWrapS;
    material.occlusionTextureWrapT = batch.occlusionTextureWrapT;
    material.occlusionTextureSrgb = batch.occlusionTextureSrgb;

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
    material.emissiveTextureMipLevels = batch.emissiveTextureMipLevels;
    material.emissiveTextureMipLevelCount = batch.emissiveTextureMipLevelCount;
    material.emissiveTextureWrapS = batch.emissiveTextureWrapS;
    material.emissiveTextureWrapT = batch.emissiveTextureWrapT;
    material.emissiveTextureSrgb = batch.emissiveTextureSrgb;

    material.environmentTextureKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::environmentTextureKey));
    material.environmentTextureCacheKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::environmentTextureCacheKey));
    assignIfPresent(
        batchTemplate,
        material.environmentTextureRgba,
        &shared_world_batches::WorldIndexedBatch::environmentTextureRgba);
    assignIfPositive(
        batchTemplate,
        material.environmentTextureWidth,
        &shared_world_batches::WorldIndexedBatch::environmentTextureWidth);
    assignIfPositive(
        batchTemplate,
        material.environmentTextureHeight,
        &shared_world_batches::WorldIndexedBatch::environmentTextureHeight);
    material.environmentTextureMipLevels = batch.environmentTextureMipLevels;
    material.environmentTextureMipLevelCount =
        batch.environmentTextureMipLevelCount;
    material.environmentTextureWrapS = batch.environmentTextureWrapS;
    material.environmentTextureWrapT = batch.environmentTextureWrapT;
    material.environmentTextureSrgb = batch.environmentTextureSrgb;

    material.lightProjectionTextureKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::
                lightProjectionTextureKey));
    material.lightProjectionTextureCacheKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::
                lightProjectionTextureCacheKey));
    assignIfPresent(
        batchTemplate,
        material.lightProjectionTextureRgba,
        &shared_world_batches::WorldIndexedBatch::
            lightProjectionTextureRgba);
    assignIfPositive(
        batchTemplate,
        material.lightProjectionTextureWidth,
        &shared_world_batches::WorldIndexedBatch::
            lightProjectionTextureWidth);
    assignIfPositive(
        batchTemplate,
        material.lightProjectionTextureHeight,
        &shared_world_batches::WorldIndexedBatch::
            lightProjectionTextureHeight);
    material.lightProjectionTextureMipLevels =
        batch.lightProjectionTextureMipLevels;
    material.lightProjectionTextureMipLevelCount =
        batch.lightProjectionTextureMipLevelCount;
    material.lightProjectionTextureWrapS =
        batch.lightProjectionTextureWrapS;
    material.lightProjectionTextureWrapT =
        batch.lightProjectionTextureWrapT;
    material.lightProjectionTextureSrgb =
        batch.lightProjectionTextureSrgb;
    material.lightProjectionUvRowU = batch.lightProjectionUvRowU;
    material.lightProjectionUvRowV = batch.lightProjectionUvRowV;

    material.projectedShadowTextureKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::
                projectedShadowTextureKey));
    material.projectedShadowTextureCacheKey = std::string(
        resolvedStringMember(
            batchTemplate,
            &shared_world_batches::WorldIndexedBatch::
                projectedShadowTextureCacheKey));
    assignIfPresent(
        batchTemplate,
        material.projectedShadowTextureRgba,
        &shared_world_batches::WorldIndexedBatch::
            projectedShadowTextureRgba);
    assignIfPositive(
        batchTemplate,
        material.projectedShadowTextureWidth,
        &shared_world_batches::WorldIndexedBatch::
            projectedShadowTextureWidth);
    assignIfPositive(
        batchTemplate,
        material.projectedShadowTextureHeight,
        &shared_world_batches::WorldIndexedBatch::
            projectedShadowTextureHeight);
    material.projectedShadowTextureWrapS =
        batch.projectedShadowTextureWrapS;
    material.projectedShadowTextureWrapT =
        batch.projectedShadowTextureWrapT;
    material.projectedShadowTextureSrgb =
        batch.projectedShadowTextureSrgb;
    material.projectedShadowEnabled = batch.projectedShadowEnabled;
    material.projectedShadowSamplingScale =
        batch.projectedShadowSamplingScale;
    material.projectedShadowBias = batch.projectedShadowBias;
    material.projectedShadowMatrix = batch.projectedShadowMatrix;

    material.alphaMode = batch.alphaMode;
    material.blendMode = batch.blendMode;
    material.dualSourceBlendEnabled = batch.dualSourceBlendEnabled;
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
    std::size_t indexCount,
    const IRenderBackend::WorldSceneSourceVertex* sourceVertices,
    std::size_t sourceVertexCount,
    std::uint32_t sourceVertexSemanticMask,
    std::uint32_t sourceMeshIndex,
    std::uint32_t sourcePolygonGroupIndex) {
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
    geometry.sourceVertices = sourceVertices;
    geometry.sourceVertexCount = sourceVertexCount;
    geometry.sourceVertexSemanticMask = sourceVertexSemanticMask;
    geometry.sourceMeshIndex = sourceMeshIndex;
    geometry.sourcePolygonGroupIndex = sourcePolygonGroupIndex;
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

shared_world_batches::WorldIndexedBatch makeWorldIndexedMaterialTemplate(
    const IRenderBackend::WorldSceneMaterial& material) {
    shared_world_batches::WorldIndexedBatch out{};
    out.textureKey = material.textureKey;
    out.textureCacheKey = material.textureCacheKey;
    out.textureRgba = material.textureRgba;
    out.textureWidth = material.textureWidth;
    out.textureHeight = material.textureHeight;
    out.textureMipLevels = material.textureMipLevels;
    out.textureMipLevelCount = material.textureMipLevelCount;
    out.textureWrapS = material.textureWrapS;
    out.textureWrapT = material.textureWrapT;
    out.textureSrgb = material.textureSrgb;

    out.normalTextureKey = material.normalTextureKey;
    out.normalTextureCacheKey = material.normalTextureCacheKey;
    out.normalTextureRgba = material.normalTextureRgba;
    out.normalTextureWidth = material.normalTextureWidth;
    out.normalTextureHeight = material.normalTextureHeight;
    out.normalTextureMipLevels = material.normalTextureMipLevels;
    out.normalTextureMipLevelCount = material.normalTextureMipLevelCount;
    out.normalTextureWrapS = material.normalTextureWrapS;
    out.normalTextureWrapT = material.normalTextureWrapT;
    out.normalTextureSrgb = material.normalTextureSrgb;

    out.metallicRoughnessTextureKey =
        material.metallicRoughnessTextureKey;
    out.metallicRoughnessTextureCacheKey =
        material.metallicRoughnessTextureCacheKey;
    out.metallicRoughnessTextureRgba =
        material.metallicRoughnessTextureRgba;
    out.metallicRoughnessTextureWidth =
        material.metallicRoughnessTextureWidth;
    out.metallicRoughnessTextureHeight =
        material.metallicRoughnessTextureHeight;
    out.metallicRoughnessTextureMipLevels =
        material.metallicRoughnessTextureMipLevels;
    out.metallicRoughnessTextureMipLevelCount =
        material.metallicRoughnessTextureMipLevelCount;
    out.metallicRoughnessTextureWrapS =
        material.metallicRoughnessTextureWrapS;
    out.metallicRoughnessTextureWrapT =
        material.metallicRoughnessTextureWrapT;
    out.metallicRoughnessTextureSrgb =
        material.metallicRoughnessTextureSrgb;

    out.occlusionTextureKey = material.occlusionTextureKey;
    out.occlusionTextureCacheKey = material.occlusionTextureCacheKey;
    out.occlusionTextureRgba = material.occlusionTextureRgba;
    out.occlusionTextureWidth = material.occlusionTextureWidth;
    out.occlusionTextureHeight = material.occlusionTextureHeight;
    out.occlusionTextureMipLevels = material.occlusionTextureMipLevels;
    out.occlusionTextureMipLevelCount =
        material.occlusionTextureMipLevelCount;
    out.occlusionTextureWrapS = material.occlusionTextureWrapS;
    out.occlusionTextureWrapT = material.occlusionTextureWrapT;
    out.occlusionTextureSrgb = material.occlusionTextureSrgb;

    out.emissiveTextureKey = material.emissiveTextureKey;
    out.emissiveTextureCacheKey = material.emissiveTextureCacheKey;
    out.emissiveTextureRgba = material.emissiveTextureRgba;
    out.emissiveTextureWidth = material.emissiveTextureWidth;
    out.emissiveTextureHeight = material.emissiveTextureHeight;
    out.emissiveTextureMipLevels = material.emissiveTextureMipLevels;
    out.emissiveTextureMipLevelCount = material.emissiveTextureMipLevelCount;
    out.emissiveTextureWrapS = material.emissiveTextureWrapS;
    out.emissiveTextureWrapT = material.emissiveTextureWrapT;
    out.emissiveTextureSrgb = material.emissiveTextureSrgb;

    out.environmentTextureKey = material.environmentTextureKey;
    out.environmentTextureCacheKey = material.environmentTextureCacheKey;
    out.environmentTextureRgba = material.environmentTextureRgba;
    out.environmentTextureWidth = material.environmentTextureWidth;
    out.environmentTextureHeight = material.environmentTextureHeight;
    out.environmentTextureMipLevels = material.environmentTextureMipLevels;
    out.environmentTextureMipLevelCount =
        material.environmentTextureMipLevelCount;
    out.environmentTextureWrapS = material.environmentTextureWrapS;
    out.environmentTextureWrapT = material.environmentTextureWrapT;
    out.environmentTextureSrgb = material.environmentTextureSrgb;

    out.lightProjectionTextureKey = material.lightProjectionTextureKey;
    out.lightProjectionTextureCacheKey =
        material.lightProjectionTextureCacheKey;
    out.lightProjectionTextureRgba = material.lightProjectionTextureRgba;
    out.lightProjectionTextureWidth = material.lightProjectionTextureWidth;
    out.lightProjectionTextureHeight = material.lightProjectionTextureHeight;
    out.lightProjectionTextureMipLevels =
        material.lightProjectionTextureMipLevels;
    out.lightProjectionTextureMipLevelCount =
        material.lightProjectionTextureMipLevelCount;
    out.lightProjectionTextureWrapS = material.lightProjectionTextureWrapS;
    out.lightProjectionTextureWrapT = material.lightProjectionTextureWrapT;
    out.lightProjectionTextureSrgb = material.lightProjectionTextureSrgb;
    out.lightProjectionUvRowU = material.lightProjectionUvRowU;
    out.lightProjectionUvRowV = material.lightProjectionUvRowV;

    out.projectedShadowTextureKey = material.projectedShadowTextureKey;
    out.projectedShadowTextureCacheKey =
        material.projectedShadowTextureCacheKey;
    out.projectedShadowTextureRgba = material.projectedShadowTextureRgba;
    out.projectedShadowTextureWidth = material.projectedShadowTextureWidth;
    out.projectedShadowTextureHeight = material.projectedShadowTextureHeight;
    out.projectedShadowTextureWrapS = material.projectedShadowTextureWrapS;
    out.projectedShadowTextureWrapT = material.projectedShadowTextureWrapT;
    out.projectedShadowTextureSrgb = material.projectedShadowTextureSrgb;
    out.projectedShadowEnabled = material.projectedShadowEnabled;
    out.projectedShadowSamplingScale =
        material.projectedShadowSamplingScale;
    out.projectedShadowBias = material.projectedShadowBias;
    out.projectedShadowMatrix = material.projectedShadowMatrix;

    out.alphaMode = material.alphaMode;
    out.blendMode = material.blendMode;
    out.dualSourceBlendEnabled = material.dualSourceBlendEnabled;
    out.materialMode = material.materialMode;
    out.alphaCutoff = material.alphaCutoff;
    out.normalScale = material.normalScale;
    out.metallicFactor = material.metallicFactor;
    out.roughnessFactor = material.roughnessFactor;
    out.occlusionStrength = material.occlusionStrength;
    out.emissiveFactorR = material.emissiveFactorR;
    out.emissiveFactorG = material.emissiveFactorG;
    out.emissiveFactorB = material.emissiveFactorB;
    if (material.materialMode == 2u || material.materialMode == 27u ||
        material.materialMode == 28u) {
        out.textureDetailLodBias = material.projectedShadowBias;
    }
    out.characterInkingEnabled = material.characterInkingEnabled;
    out.materialTimeSec = material.materialTimeSec;
    out.materialFlags = material.materialFlags;
    out.materialAtlasWidth = material.materialAtlasWidth;
    out.materialAtlasHeight = material.materialAtlasHeight;
    out.materialRect0U = material.materialRect0U;
    out.materialRect0V = material.materialRect0V;
    out.materialRect0W = material.materialRect0W;
    out.materialRect0H = material.materialRect0H;
    out.materialRect1U = material.materialRect1U;
    out.materialRect1V = material.materialRect1V;
    out.materialRect1W = material.materialRect1W;
    out.materialRect1H = material.materialRect1H;
    out.materialFlipbook0Cols = material.materialFlipbook0Cols;
    out.materialFlipbook0Rows = material.materialFlipbook0Rows;
    out.materialFlipbook0Frames = material.materialFlipbook0Frames;
    out.materialFlipbook0Fps = material.materialFlipbook0Fps;
    out.materialFlipbook1Cols = material.materialFlipbook1Cols;
    out.materialFlipbook1Rows = material.materialFlipbook1Rows;
    out.materialFlipbook1Frames = material.materialFlipbook1Frames;
    out.materialFlipbook1Fps = material.materialFlipbook1Fps;
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
    tex.mipLevels = material.textureMipLevels;
    tex.mipLevelCount = material.textureMipLevelCount;
    tex.wrapS = material.textureWrapS;
    tex.wrapT = material.textureWrapT;
    tex.textureSrgb = material.textureSrgb;
    tex.normalKey =
        material.normalTextureKey.empty() ? "" : material.normalTextureKey.c_str();
    tex.normalCacheKey = material.normalTextureCacheKey.empty()
        ? nullptr
        : material.normalTextureCacheKey.c_str();
    tex.normalRgba = material.normalTextureRgba;
    tex.normalWidth = material.normalTextureWidth;
    tex.normalHeight = material.normalTextureHeight;
    tex.normalMipLevels = material.normalTextureMipLevels;
    tex.normalMipLevelCount = material.normalTextureMipLevelCount;
    tex.normalWrapS = material.normalTextureWrapS;
    tex.normalWrapT = material.normalTextureWrapT;
    tex.normalTextureSrgb = material.normalTextureSrgb;
    tex.metallicRoughnessKey = material.metallicRoughnessTextureKey.empty()
        ? ""
        : material.metallicRoughnessTextureKey.c_str();
    tex.metallicRoughnessCacheKey = material.metallicRoughnessTextureCacheKey.empty()
        ? nullptr
        : material.metallicRoughnessTextureCacheKey.c_str();
    tex.metallicRoughnessRgba = material.metallicRoughnessTextureRgba;
    tex.metallicRoughnessWidth = material.metallicRoughnessTextureWidth;
    tex.metallicRoughnessHeight = material.metallicRoughnessTextureHeight;
    tex.metallicRoughnessMipLevels =
        material.metallicRoughnessTextureMipLevels;
    tex.metallicRoughnessMipLevelCount =
        material.metallicRoughnessTextureMipLevelCount;
    tex.metallicRoughnessWrapS = material.metallicRoughnessTextureWrapS;
    tex.metallicRoughnessWrapT = material.metallicRoughnessTextureWrapT;
    tex.metallicRoughnessTextureSrgb =
        material.metallicRoughnessTextureSrgb;
    tex.occlusionKey =
        material.occlusionTextureKey.empty() ? "" : material.occlusionTextureKey.c_str();
    tex.occlusionCacheKey = material.occlusionTextureCacheKey.empty()
        ? nullptr
        : material.occlusionTextureCacheKey.c_str();
    tex.occlusionRgba = material.occlusionTextureRgba;
    tex.occlusionWidth = material.occlusionTextureWidth;
    tex.occlusionHeight = material.occlusionTextureHeight;
    tex.occlusionMipLevels = material.occlusionTextureMipLevels;
    tex.occlusionMipLevelCount = material.occlusionTextureMipLevelCount;
    tex.occlusionWrapS = material.occlusionTextureWrapS;
    tex.occlusionWrapT = material.occlusionTextureWrapT;
    tex.occlusionTextureSrgb = material.occlusionTextureSrgb;
    tex.emissiveKey =
        material.emissiveTextureKey.empty() ? "" : material.emissiveTextureKey.c_str();
    tex.emissiveCacheKey = material.emissiveTextureCacheKey.empty()
        ? nullptr
        : material.emissiveTextureCacheKey.c_str();
    tex.emissiveRgba = material.emissiveTextureRgba;
    tex.emissiveWidth = material.emissiveTextureWidth;
    tex.emissiveHeight = material.emissiveTextureHeight;
    tex.emissiveMipLevels = material.emissiveTextureMipLevels;
    tex.emissiveMipLevelCount = material.emissiveTextureMipLevelCount;
    tex.emissiveWrapS = material.emissiveTextureWrapS;
    tex.emissiveWrapT = material.emissiveTextureWrapT;
    tex.emissiveTextureSrgb = material.emissiveTextureSrgb;
    tex.environmentKey = material.environmentTextureKey.empty()
        ? ""
        : material.environmentTextureKey.c_str();
    tex.environmentCacheKey = material.environmentTextureCacheKey.empty()
        ? nullptr
        : material.environmentTextureCacheKey.c_str();
    tex.environmentRgba = material.environmentTextureRgba;
    tex.environmentWidth = material.environmentTextureWidth;
    tex.environmentHeight = material.environmentTextureHeight;
    tex.environmentMipLevels = material.environmentTextureMipLevels;
    tex.environmentMipLevelCount = material.environmentTextureMipLevelCount;
    tex.environmentWrapS = material.environmentTextureWrapS;
    tex.environmentWrapT = material.environmentTextureWrapT;
    tex.environmentTextureSrgb = material.environmentTextureSrgb;
    tex.lightProjectionKey = material.lightProjectionTextureKey.empty()
        ? ""
        : material.lightProjectionTextureKey.c_str();
    tex.lightProjectionCacheKey =
        material.lightProjectionTextureCacheKey.empty()
        ? nullptr
        : material.lightProjectionTextureCacheKey.c_str();
    tex.lightProjectionRgba = material.lightProjectionTextureRgba;
    tex.lightProjectionWidth = material.lightProjectionTextureWidth;
    tex.lightProjectionHeight = material.lightProjectionTextureHeight;
    tex.lightProjectionMipLevels =
        material.lightProjectionTextureMipLevels;
    tex.lightProjectionMipLevelCount =
        material.lightProjectionTextureMipLevelCount;
    tex.lightProjectionWrapS = material.lightProjectionTextureWrapS;
    tex.lightProjectionWrapT = material.lightProjectionTextureWrapT;
    tex.lightProjectionTextureSrgb =
        material.lightProjectionTextureSrgb;
    tex.lightProjectionUvRowU = material.lightProjectionUvRowU;
    tex.lightProjectionUvRowV = material.lightProjectionUvRowV;
    tex.projectedShadowKey = material.projectedShadowTextureKey.empty()
        ? ""
        : material.projectedShadowTextureKey.c_str();
    tex.projectedShadowCacheKey =
        material.projectedShadowTextureCacheKey.empty()
        ? nullptr
        : material.projectedShadowTextureCacheKey.c_str();
    tex.projectedShadowRgba = material.projectedShadowTextureRgba;
    tex.projectedShadowWidth = material.projectedShadowTextureWidth;
    tex.projectedShadowHeight = material.projectedShadowTextureHeight;
    tex.projectedShadowWrapS = material.projectedShadowTextureWrapS;
    tex.projectedShadowWrapT = material.projectedShadowTextureWrapT;
    tex.projectedShadowTextureSrgb =
        material.projectedShadowTextureSrgb;
    tex.projectedShadowEnabled = material.projectedShadowEnabled;
    tex.projectedShadowSamplingScale =
        material.projectedShadowSamplingScale;
    tex.projectedShadowBias = material.projectedShadowBias;
    tex.projectedShadowMatrix = material.projectedShadowMatrix;
    tex.alphaMode = material.alphaMode;
    tex.blendMode = material.blendMode;
    tex.dualSourceBlendEnabled = material.dualSourceBlendEnabled;
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
