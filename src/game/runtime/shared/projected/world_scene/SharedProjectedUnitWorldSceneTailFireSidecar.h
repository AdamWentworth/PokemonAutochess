#pragma once

#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneBatchState.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"

#include <vector>

namespace game::runtime::shared_projected_unit_world_scene::tail_fire_sidecar {

bool buildTailFireSidecarBatches(
    const game::runtime::shared_projected_unit_models::Args& args,
    const game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState& prepared,
    const game::runtime::shared_projected_unit_backend_mesh_support::FastTexturedMeshTemplateCache&
        fastCache,
    const game::runtime::shared_tail_fire_mesh_playback::Profile& profile,
    const game::runtime::shared_projected_unit_world_scene::batch_state::ResolvedBatchState&
        batchState,
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>& outBatches);

} // namespace game::runtime::shared_projected_unit_world_scene::tail_fire_sidecar

