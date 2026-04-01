#pragma once

#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h"

#include <cstdint>
#include <vector>

namespace game::runtime::shared_projected_unit_backend_mesh_gpu_skin_batch_state {

shared_projected_unit_backend_mesh_support::GpuSkinBatchStateEntry* resolveGpuSkinBatchState(
    std::vector<shared_projected_unit_backend_mesh_support::GpuSkinBatchStateEntry>&
        gpuSkinBatchStates,
    shared_projected_unit_backend_mesh_support::GpuSkinBatchStateEntry*& lastGpuSkinBatchState,
    shared_projected_unit_backend_mesh_transforms::Resolver& transforms,
    int unitId,
    int resolvedTriNodeIndex,
    int skinCacheKey,
    const std::vector<std::uint16_t>* jointPalette);

} // namespace game::runtime::shared_projected_unit_backend_mesh_gpu_skin_batch_state
