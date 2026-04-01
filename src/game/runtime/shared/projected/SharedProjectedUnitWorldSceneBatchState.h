#pragma once

#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h"
#include "game/runtime/shared/projected/SharedProjectedUnitModelRenderer.h"

#include <cstdint>
#include <vector>

namespace game::runtime::shared_projected_unit_world_scene::batch_state {

struct ResolvedBatchState {
    std::vector<game::runtime::shared_projected_unit_backend_mesh_support::GpuSkinBatchState>
        batchSkinStates;
    std::vector<std::uint8_t> batchUsesSceneSkinning;
    std::vector<int> resolvedTriNodeIndices;
    game::runtime::shared_projected_unit_backend_mesh_transforms::Resolver transforms;
    bool transformsInitialized = false;

    void ensureTransformsInitialized(
        const game::runtime::shared_projected_unit_models::Args& args,
        const game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState& prepared);
};

bool resolveBatchState(
    const game::runtime::shared_projected_unit_models::Args& args,
    const game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState& prepared,
    const game::runtime::shared_projected_unit_backend_mesh_support::FastTexturedMeshTemplateCache&
        fastCache,
    const IRenderBackend::WorldSceneFastPathCaps& fastPathCaps,
    bool enableGpuClipSkinning,
    ResolvedBatchState& out);

} // namespace game::runtime::shared_projected_unit_world_scene::batch_state
