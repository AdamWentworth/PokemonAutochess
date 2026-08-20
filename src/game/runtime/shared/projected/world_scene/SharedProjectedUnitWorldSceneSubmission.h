#pragma once

#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneBatchState.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace game::runtime::shared_projected_unit_world_scene::submission {

struct SubmissionSummary {
    std::size_t rigidBatchCount = 0u;
    std::size_t skinnedBatchCount = 0u;
    std::uint64_t batchHash = 0u;
};

std::array<float, 16> buildRigidBatchModelMatrix(
    const game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState& prepared,
    const game::runtime::shared_backend_pose::PoseEval* scenePose,
    int resolvedTriNodeIndex);

SubmissionSummary appendWorldSceneInstances(
    const game::runtime::shared_projected_unit_models::Args& args,
    const game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState& prepared,
    const game::runtime::shared_projected_unit_backend_mesh_support::FastTexturedMeshTemplateCache&
        fastCache,
    const game::runtime::shared_projected_unit_backend_mesh_support::
        FastTexturedMaterialTemplateCache& materialCache,
    const game::runtime::shared_projected_unit_world_scene::batch_state::ResolvedBatchState&
        batchState,
    bool traceThisUnit);

} // namespace game::runtime::shared_projected_unit_world_scene::submission

