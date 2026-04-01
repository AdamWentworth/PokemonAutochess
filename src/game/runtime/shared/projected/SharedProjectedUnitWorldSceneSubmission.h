#pragma once

#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/SharedProjectedUnitWorldSceneBatchState.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace game::runtime::shared_projected_unit_world_scene::submission {

struct SubmissionSummary {
    std::size_t rigidBatchCount = 0u;
    std::size_t skinnedBatchCount = 0u;
    std::uint64_t batchHash = 0u;
};

bool batchUsesTailFireSubmesh(
    const game::runtime::shared_projected_unit_backend_mesh_support::FastTexturedBatchTemplate&
        batchTemplate,
    const game::runtime::shared_tail_fire_mesh_playback::Profile* profile);

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
    const game::runtime::shared_tail_fire_mesh_playback::Profile* tailFireProfile,
    const game::runtime::shared_projected_unit_world_scene::batch_state::ResolvedBatchState&
        batchState,
    bool traceThisUnit);

} // namespace game::runtime::shared_projected_unit_world_scene::submission
