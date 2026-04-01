#pragma once

#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/projected/core/SharedProjectedRenderItems.h"

#include <cstdint>

namespace game::runtime::shared_projected_unit_backend_mesh_persistent {

struct SyncContext {
    shared_projected_render_items::ProjectedRenderItemRegistry* projectedRenderItems = nullptr;
    const runtime::render_model::MeshData* mesh = nullptr;
    int unitId = 0;
    std::uint32_t frameStamp = 0u;
};

std::uint64_t hashScenePoseEval(const runtime::shared_backend_pose::PoseEval* scenePose);

shared_projected_render_items::ProjectedRenderItemEntry* ensurePersistentRenderItem(
    const SyncContext& context,
    std::uint32_t itemIndex);

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
    const void* geometryTemplateIdentity);

} // namespace game::runtime::shared_projected_unit_backend_mesh_persistent

