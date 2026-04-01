#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace game::runtime::render_model {
struct MeshData;
}

namespace game::runtime::shared_projected_render_items {

struct ProjectedRenderItemKey {
    int unitId = 0;
    const runtime::render_model::MeshData* mesh = nullptr;
    std::uint32_t itemIndex = 0u;

    bool operator==(const ProjectedRenderItemKey& other) const;
};

struct ProjectedRenderItemKeyHash {
    std::size_t operator()(const ProjectedRenderItemKey& key) const noexcept;
};

enum class ProjectedRenderItemDirtyBits : std::uint32_t {
    None = 0u,
    StaticTemplate = 1u << 0,
    MaterialIdentity = 1u << 1,
    GeometryIdentity = 1u << 2,
    DynamicTransform = 1u << 3,
    DynamicMaterial = 1u << 4,
    DynamicSkinning = 1u << 5,
    Visibility = 1u << 6,
};

struct ProjectedRenderItemStaticData {
    std::uint32_t baseSubmeshIndex = 0u;
    int triNodeIndex = -1;
    int meshNodeIndex = -1;
    std::uint8_t skinnedBatch = 0u;
    std::uint8_t canUseSharedNodeTransform = 0u;
    std::uint8_t hasStableGpuTemplate = 0u;

    std::string geometryCacheKey;

    std::string textureKey;
    std::string textureCacheKey;
    std::string normalTextureKey;
    std::string normalTextureCacheKey;
    std::string metallicRoughnessTextureKey;
    std::string metallicRoughnessTextureCacheKey;
    std::string occlusionTextureKey;
    std::string occlusionTextureCacheKey;
    std::string emissiveTextureKey;
    std::string emissiveTextureCacheKey;

    std::uint8_t materialMode = 0u;
    std::uint8_t alphaMode = 0u;
    std::uint8_t blendMode = 0u;
    std::uint8_t characterInkingEnabled = 0u;

    const IRenderBackend::WorldMeshVertex* sharedVertices = nullptr;
    std::size_t sharedVertexCount = 0u;
    const std::uint32_t* sharedIndices = nullptr;
    std::size_t sharedIndexCount = 0u;
    const void* geometryTemplateIdentity = nullptr;
    const shared_world_batches::WorldIndexedBatch* materialTemplateIdentity = nullptr;
};

struct ProjectedRenderItemDynamicData {
    std::array<float, 16> modelMatrix{};
    float sortDepth = 0.0f;
    float vertexColorMulR = 1.0f;
    float vertexColorMulG = 1.0f;
    float vertexColorMulB = 1.0f;
    float vertexColorMulA = 1.0f;

    std::uint8_t gpuSkinning = 0u;
    std::uint8_t gpuSkinningMode = 0u;
    std::uint32_t skinMatrixCount = 0u;
    const float* sharedSkinMatrices = nullptr;

    std::uint8_t materialAlphaOverride = 0u;
    float alphaCutoff = 0.0f;
    std::uint32_t visibilityFrameStamp = 0u;
};

struct ProjectedRenderItemEntry {
    ProjectedRenderItemKey key{};
    ProjectedRenderItemStaticData staticData{};
    ProjectedRenderItemDynamicData dynamicData{};
    const void* cpuRewriteGeometryTemplateIdentity = nullptr;
    std::uint64_t cpuRewritePoseHash = 0ull;
    std::uint8_t cpuRewriteNeedsLitNormals = 0u;
    std::uint8_t cpuRewriteNeedsTangents = 0u;
    std::vector<IRenderBackend::WorldMeshVertex> cpuRewriteVertices;
    std::uint32_t dirtyBits = 0u;
    std::uint32_t lastTouchedFrame = 0u;
    IRenderBackend::WorldSceneRenderObjectHandle worldSceneObjectHandle{};
    std::uint32_t worldSceneRegistryGeneration = 0u;
};

struct ProjectedRenderItemRegistry {
    std::unordered_map<ProjectedRenderItemKey,
                       ProjectedRenderItemEntry,
                       ProjectedRenderItemKeyHash>
        entries;
    std::uint32_t currentFrameId = 0u;
    std::uint32_t pruneAfterFrames = 120u;
};

std::uint32_t dirtyBit(ProjectedRenderItemDirtyBits bit);
void resetProjectedRenderItems(ProjectedRenderItemRegistry& registry);
void beginProjectedRenderItemsFrame(ProjectedRenderItemRegistry& registry);

ProjectedRenderItemEntry& ensureProjectedRenderItem(
    ProjectedRenderItemRegistry& registry,
    const ProjectedRenderItemKey& key);

void markProjectedRenderItemDirty(ProjectedRenderItemEntry& entry,
                                  ProjectedRenderItemDirtyBits bit);
void clearProjectedRenderItemDirty(ProjectedRenderItemEntry& entry,
                                   ProjectedRenderItemDirtyBits bit);
void touchProjectedRenderItem(ProjectedRenderItemRegistry& registry,
                              ProjectedRenderItemEntry& entry);

void syncProjectedRenderItemStaticTemplate(
    ProjectedRenderItemEntry& entry,
    const shared_world_batches::WorldIndexedBatch& batch,
    std::uint32_t baseSubmeshIndex,
    int triNodeIndex,
    int meshNodeIndex,
    bool skinnedBatch,
    bool canUseSharedNodeTransform,
    bool hasStableGpuTemplate,
    const void* geometryTemplateIdentity);

void syncProjectedRenderItemDynamicState(
    ProjectedRenderItemEntry& entry,
    const shared_world_batches::WorldIndexedBatch& batch,
    std::uint32_t visibilityFrameStamp);

} // namespace game::runtime::shared_projected_render_items
