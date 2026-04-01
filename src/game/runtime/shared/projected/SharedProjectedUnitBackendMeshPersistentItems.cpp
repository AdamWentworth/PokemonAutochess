#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPersistentItems.h"

#include <glm/gtc/type_ptr.hpp>

namespace persistent = game::runtime::shared_projected_render_items;

namespace game::runtime::shared_projected_unit_backend_mesh_persistent {

namespace {

std::uint64_t fnv1a64Append(std::uint64_t hash, const void* data, std::size_t byteCount) {
    static constexpr std::uint64_t kPrime = 1099511628211ull;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < byteCount; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= kPrime;
    }
    return hash;
}

} // namespace

std::uint64_t hashScenePoseEval(
    const runtime::shared_backend_pose::PoseEval* scenePose) {
    if (!scenePose || !scenePose->hasScenePose || scenePose->nodeGlobals.empty()) {
        return 0ull;
    }

    std::uint64_t hash = 14695981039346656037ull;
    for (const glm::mat4& nodeGlobal : scenePose->nodeGlobals) {
        hash = fnv1a64Append(hash, glm::value_ptr(nodeGlobal), sizeof(float) * 16u);
    }
    return hash;
}

persistent::ProjectedRenderItemEntry* ensurePersistentRenderItem(
    const SyncContext& context,
    std::uint32_t itemIndex) {
    if (!context.projectedRenderItems || !context.mesh) {
        return nullptr;
    }

    persistent::ProjectedRenderItemKey key{};
    key.unitId = context.unitId;
    key.mesh = context.mesh;
    key.itemIndex = itemIndex;
    return &persistent::ensureProjectedRenderItem(*context.projectedRenderItems, key);
}

void syncPersistentRenderItem(
    const SyncContext& context,
    std::uint32_t itemIndex,
    const shared_world_batches::WorldIndexedBatch& batch,
    std::uint32_t baseSubmeshIndex,
    int triNodeIndex,
    int meshNodeIndex,
    bool skinnedBatch,
    bool canUseSharedNodeTransform,
    bool hasStableGpuTemplate,
    const void* geometryTemplateIdentity) {
    if (!context.projectedRenderItems || !context.mesh) {
        return;
    }

    persistent::ProjectedRenderItemKey key{};
    key.unitId = context.unitId;
    key.mesh = context.mesh;
    key.itemIndex = itemIndex;
    auto& entry =
        persistent::ensureProjectedRenderItem(*context.projectedRenderItems, key);
    persistent::touchProjectedRenderItem(*context.projectedRenderItems, entry);
    persistent::syncProjectedRenderItemStaticTemplate(
        entry,
        batch,
        baseSubmeshIndex,
        triNodeIndex,
        meshNodeIndex,
        skinnedBatch,
        canUseSharedNodeTransform,
        hasStableGpuTemplate,
        geometryTemplateIdentity);
    persistent::syncProjectedRenderItemDynamicState(
        entry,
        batch,
        context.frameStamp);
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_persistent
