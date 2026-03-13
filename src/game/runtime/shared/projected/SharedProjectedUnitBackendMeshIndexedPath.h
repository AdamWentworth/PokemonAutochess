#pragma once

#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h"

namespace game::runtime::shared_projected_unit_backend_mesh_indexed_path {

struct FastTexturedPathArgs {
    const shared_projected_unit_backend_mesh::Args& renderArgs;
    const PokemonInstance& unit;
    const runtime::render_model::MeshData& mesh;
    const std::vector<glm::mat4>& nodeGlobals;
    const std::vector<int>& submeshNodeFallback;
    const shared_projected_unit_backend_mesh_support::FastTexturedMeshTemplateCache& fastCache;
    shared_projected_unit_backend_mesh_prep::PreparedState& prep;
    shared_projected_unit_backend_mesh_transforms::Resolver& transforms;
};

struct QueueIndexedWorldBatchesArgs {
    const shared_projected_unit_backend_mesh::Args& renderArgs;
    const runtime::render_model::MeshData& mesh;
    std::vector<shared_world_batches::WorldIndexedBatch>& modelIndexedBatchesPerSubmesh;
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches;
};

void clearIndexedBatchDynamicState(
    std::vector<shared_world_batches::WorldIndexedBatch>& batches);
void invalidateIndexedBatches(
    std::vector<shared_world_batches::WorldIndexedBatch>& batches);
bool applyFastTexturedPath(const FastTexturedPathArgs& args);
bool queueIndexedWorldBatches(const QueueIndexedWorldBatchesArgs& args);

} // namespace game::runtime::shared_projected_unit_backend_mesh_indexed_path
