#include "game/runtime/shared/projected/core/SharedProjectedRenderItems.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>

namespace game::runtime::shared_projected_render_items {

namespace {

const shared_world_batches::WorldIndexedBatch& materialBatchOrSelf(
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
bool assignIfChanged(T& dst, const T& src) {
    if (dst == src) return false;
    dst = src;
    return true;
}

bool assignStringIfChanged(std::string& dst, std::string_view src) {
    if (dst.size() == src.size() &&
        std::equal(dst.begin(), dst.end(), src.begin(), src.end())) {
        return false;
    }
    dst.assign(src.begin(), src.end());
    return true;
}

bool assignModelMatrixIfChanged(std::array<float, 16>& dst,
                                const std::array<float, 16>& src) {
    if (std::memcmp(dst.data(), src.data(), sizeof(float) * dst.size()) == 0) {
        return false;
    }
    dst = src;
    return true;
}

} // namespace

bool ProjectedRenderItemKey::operator==(const ProjectedRenderItemKey& other) const {
    return unitId == other.unitId &&
           mesh == other.mesh &&
           itemIndex == other.itemIndex &&
           materialVariant == other.materialVariant;
}

std::size_t ProjectedRenderItemKeyHash::operator()(
    const ProjectedRenderItemKey& key) const noexcept {
    std::size_t h = static_cast<std::size_t>(static_cast<std::uint32_t>(key.unitId));
    h ^= reinterpret_cast<std::size_t>(key.mesh) + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= static_cast<std::size_t>(key.itemIndex) + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= static_cast<std::size_t>(key.materialVariant) +
         0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}

std::uint32_t dirtyBit(ProjectedRenderItemDirtyBits bit) {
    return static_cast<std::uint32_t>(bit);
}

void resetProjectedRenderItems(ProjectedRenderItemRegistry& registry) {
    registry.entries.clear();
    registry.currentFrameId = 0u;
}

void beginProjectedRenderItemsFrame(ProjectedRenderItemRegistry& registry) {
    if (registry.currentFrameId == std::numeric_limits<std::uint32_t>::max()) {
        registry.currentFrameId = 1u;
    } else {
        ++registry.currentFrameId;
        if (registry.currentFrameId == 0u) {
            registry.currentFrameId = 1u;
        }
    }

    if (registry.entries.empty()) return;

    const std::uint32_t frameId = registry.currentFrameId;
    const std::uint32_t pruneAfter = std::max<std::uint32_t>(1u, registry.pruneAfterFrames);
    for (auto it = registry.entries.begin(); it != registry.entries.end();) {
        const std::uint32_t age = frameId - it->second.lastTouchedFrame;
        if (it->second.lastTouchedFrame == 0u || age > pruneAfter) {
            it = registry.entries.erase(it);
        } else {
            ++it;
        }
    }
}

ProjectedRenderItemEntry& ensureProjectedRenderItem(
    ProjectedRenderItemRegistry& registry,
    const ProjectedRenderItemKey& key) {
    auto it = registry.entries.find(key);
    if (it != registry.entries.end()) {
        return it->second;
    }

    ProjectedRenderItemEntry entry{};
    entry.key = key;
    entry.dirtyBits =
        dirtyBit(ProjectedRenderItemDirtyBits::StaticTemplate) |
        dirtyBit(ProjectedRenderItemDirtyBits::MaterialIdentity) |
        dirtyBit(ProjectedRenderItemDirtyBits::GeometryIdentity) |
        dirtyBit(ProjectedRenderItemDirtyBits::DynamicTransform) |
        dirtyBit(ProjectedRenderItemDirtyBits::DynamicMaterial) |
        dirtyBit(ProjectedRenderItemDirtyBits::DynamicSkinning) |
        dirtyBit(ProjectedRenderItemDirtyBits::Visibility);
    entry.lastTouchedFrame = registry.currentFrameId;
    const auto [insertedIt, _] = registry.entries.emplace(key, std::move(entry));
    return insertedIt->second;
}

void markProjectedRenderItemDirty(ProjectedRenderItemEntry& entry,
                                  ProjectedRenderItemDirtyBits bit) {
    entry.dirtyBits |= dirtyBit(bit);
}

void clearProjectedRenderItemDirty(ProjectedRenderItemEntry& entry,
                                   ProjectedRenderItemDirtyBits bit) {
    entry.dirtyBits &= ~dirtyBit(bit);
}

void touchProjectedRenderItem(ProjectedRenderItemRegistry& registry,
                              ProjectedRenderItemEntry& entry) {
    entry.lastTouchedFrame = registry.currentFrameId;
    entry.dynamicData.visibilityFrameStamp = registry.currentFrameId;
    markProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::Visibility);
}

void syncProjectedRenderItemStaticTemplate(
    ProjectedRenderItemEntry& entry,
    const shared_world_batches::WorldIndexedBatch& batch,
    std::uint32_t baseSubmeshIndex,
    int triNodeIndex,
    int meshNodeIndex,
    bool skinnedBatch,
    bool canUseSharedNodeTransform,
    bool hasStableGpuTemplate,
    const void* geometryTemplateIdentity) {
    const auto& materialBatch = materialBatchOrSelf(batch);

    bool geometryChanged = false;
    bool materialChanged = false;
    bool staticChanged = false;

    staticChanged |= assignIfChanged(entry.staticData.baseSubmeshIndex, baseSubmeshIndex);
    staticChanged |= assignIfChanged(entry.staticData.triNodeIndex, triNodeIndex);
    staticChanged |= assignIfChanged(entry.staticData.meshNodeIndex, meshNodeIndex);
    staticChanged |= assignIfChanged(
        entry.staticData.skinnedBatch, static_cast<std::uint8_t>(skinnedBatch ? 1u : 0u));
    staticChanged |= assignIfChanged(
        entry.staticData.canUseSharedNodeTransform,
        static_cast<std::uint8_t>(canUseSharedNodeTransform ? 1u : 0u));
    staticChanged |= assignIfChanged(
        entry.staticData.hasStableGpuTemplate,
        static_cast<std::uint8_t>(hasStableGpuTemplate ? 1u : 0u));

    const bool geometryTemplateStable =
        geometryTemplateIdentity != nullptr &&
        entry.staticData.geometryTemplateIdentity == geometryTemplateIdentity;
    if (!geometryTemplateStable) {
        geometryChanged |=
            assignStringIfChanged(entry.staticData.geometryCacheKey, batch.geometryCacheKey);
        geometryChanged |= assignIfChanged(entry.staticData.sharedVertices, batch.sharedVertices);
        geometryChanged |=
            assignIfChanged(entry.staticData.sharedVertexCount, batch.sharedVertexCount);
        geometryChanged |= assignIfChanged(entry.staticData.sharedIndices, batch.sharedIndices);
        geometryChanged |=
            assignIfChanged(entry.staticData.sharedIndexCount, batch.sharedIndexCount);
    }
    assignIfChanged(entry.staticData.geometryTemplateIdentity, geometryTemplateIdentity);

    const bool materialTemplateStable =
        batch.sharedTemplate != nullptr &&
        entry.staticData.materialTemplateIdentity == batch.sharedTemplate;
    if (!materialTemplateStable) {
        materialChanged |= assignStringIfChanged(
            entry.staticData.textureKey,
            resolvedStringMember(batch, &shared_world_batches::WorldIndexedBatch::textureKey));
        materialChanged |= assignStringIfChanged(
            entry.staticData.textureCacheKey,
            resolvedStringMember(batch, &shared_world_batches::WorldIndexedBatch::textureCacheKey));
        materialChanged |= assignStringIfChanged(
            entry.staticData.normalTextureKey,
            resolvedStringMember(batch, &shared_world_batches::WorldIndexedBatch::normalTextureKey));
        materialChanged |= assignStringIfChanged(
            entry.staticData.normalTextureCacheKey,
            resolvedStringMember(
                batch,
                &shared_world_batches::WorldIndexedBatch::normalTextureCacheKey));
        materialChanged |= assignStringIfChanged(
            entry.staticData.metallicRoughnessTextureKey,
            resolvedStringMember(
                batch,
                &shared_world_batches::WorldIndexedBatch::metallicRoughnessTextureKey));
        materialChanged |= assignStringIfChanged(
            entry.staticData.metallicRoughnessTextureCacheKey,
            resolvedStringMember(
                batch,
                &shared_world_batches::WorldIndexedBatch::metallicRoughnessTextureCacheKey));
        materialChanged |= assignStringIfChanged(
            entry.staticData.occlusionTextureKey,
            resolvedStringMember(
                batch,
                &shared_world_batches::WorldIndexedBatch::occlusionTextureKey));
        materialChanged |= assignStringIfChanged(
            entry.staticData.occlusionTextureCacheKey,
            resolvedStringMember(
                batch,
                &shared_world_batches::WorldIndexedBatch::occlusionTextureCacheKey));
        materialChanged |= assignStringIfChanged(
            entry.staticData.emissiveTextureKey,
            resolvedStringMember(
                batch,
                &shared_world_batches::WorldIndexedBatch::emissiveTextureKey));
        materialChanged |= assignStringIfChanged(
            entry.staticData.emissiveTextureCacheKey,
            resolvedStringMember(
                batch,
                &shared_world_batches::WorldIndexedBatch::emissiveTextureCacheKey));

        materialChanged |= assignIfChanged(entry.staticData.materialMode, materialBatch.materialMode);
        materialChanged |= assignIfChanged(entry.staticData.alphaMode, materialBatch.alphaMode);
        materialChanged |= assignIfChanged(entry.staticData.blendMode, materialBatch.blendMode);
        materialChanged |= assignIfChanged(
            entry.staticData.characterInkingEnabled,
            materialBatch.characterInkingEnabled);
    }
    assignIfChanged(entry.staticData.materialTemplateIdentity, batch.sharedTemplate);

    if (staticChanged) {
        markProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::StaticTemplate);
    } else {
        clearProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::StaticTemplate);
    }
    if (geometryChanged) {
        markProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::GeometryIdentity);
    } else {
        clearProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::GeometryIdentity);
    }
    if (materialChanged) {
        markProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::MaterialIdentity);
    } else {
        clearProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::MaterialIdentity);
    }
}

void syncProjectedRenderItemDynamicState(
    ProjectedRenderItemEntry& entry,
    const shared_world_batches::WorldIndexedBatch& batch,
    std::uint32_t visibilityFrameStamp) {
    bool transformChanged = false;
    bool materialChanged = false;
    bool skinningChanged = false;

    transformChanged |= assignModelMatrixIfChanged(entry.dynamicData.modelMatrix, batch.modelMatrix);
    transformChanged |= assignIfChanged(entry.dynamicData.sortDepth, batch.sortDepth);
    transformChanged |= assignIfChanged(entry.dynamicData.vertexColorMulR, batch.vertexColorMulR);
    transformChanged |= assignIfChanged(entry.dynamicData.vertexColorMulG, batch.vertexColorMulG);
    transformChanged |= assignIfChanged(entry.dynamicData.vertexColorMulB, batch.vertexColorMulB);
    transformChanged |= assignIfChanged(entry.dynamicData.vertexColorMulA, batch.vertexColorMulA);

    materialChanged |= assignIfChanged(
        entry.dynamicData.materialAlphaOverride,
        static_cast<std::uint8_t>(batch.materialAlphaOverride ? 1u : 0u));
    materialChanged |= assignIfChanged(entry.dynamicData.alphaCutoff, batch.alphaCutoff);

    skinningChanged |= assignIfChanged(entry.dynamicData.gpuSkinning, batch.gpuSkinning);
    skinningChanged |= assignIfChanged(entry.dynamicData.gpuSkinningMode, batch.gpuSkinningMode);
    skinningChanged |= assignIfChanged(entry.dynamicData.skinMatrixCount, batch.skinMatrixCount);
    skinningChanged |= assignIfChanged(
        entry.dynamicData.sharedSkinMatrices, batch.sharedSkinMatrices);

    const bool visibilityChanged =
        assignIfChanged(entry.dynamicData.visibilityFrameStamp, visibilityFrameStamp);

    if (transformChanged) {
        markProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::DynamicTransform);
    } else {
        clearProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::DynamicTransform);
    }
    if (materialChanged) {
        markProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::DynamicMaterial);
    } else {
        clearProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::DynamicMaterial);
    }
    if (skinningChanged) {
        markProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::DynamicSkinning);
    } else {
        clearProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::DynamicSkinning);
    }
    if (visibilityChanged) {
        markProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::Visibility);
    } else {
        clearProjectedRenderItemDirty(entry, ProjectedRenderItemDirtyBits::Visibility);
    }
}

} // namespace game::runtime::shared_projected_render_items

