#pragma once

#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPersistentItems.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"

#include <cstdint>
#include <vector>

namespace game::runtime::shared_projected_unit_backend_mesh_indexed_finalize {

struct Args {
    const shared_projected_unit_backend_mesh::Args* renderArgs = nullptr;
    const runtime::render_model::MeshData* mesh = nullptr;
    const std::vector<int>* submeshNodeFallback = nullptr;
    const shared_projected_unit_backend_mesh_support::FastTexturedMeshTemplateCache* fastCache =
        nullptr;
    const shared_projected_unit_backend_mesh_persistent::SyncContext* persistentSync = nullptr;

    std::vector<shared_world_batches::WorldIndexedBatch>* modelIndexedBatchesPerSubmesh = nullptr;
    std::vector<std::uint8_t>* batchUsesGpuClipPalette = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
};

struct Result {
    bool queuedIndexedBatch = false;
    std::uint32_t sharedRigidBatches = 0u;
    std::uint32_t gpuClipSkinBatches = 0u;
    std::uint32_t gpuClipPaletteBatches = 0u;
    std::uint32_t cpuRewriteBatches = 0u;
    std::uint32_t indexedBatchesQueued = 0u;
};

Result finalizeIndexedWorldBatches(const Args& args);

} // namespace game::runtime::shared_projected_unit_backend_mesh_indexed_finalize

