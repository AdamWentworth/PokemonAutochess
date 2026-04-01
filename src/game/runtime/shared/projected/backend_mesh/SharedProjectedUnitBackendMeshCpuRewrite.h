#pragma once

#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPersistentItems.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTransforms.h"

#include <cstdint>

namespace game::runtime::shared_projected_unit_backend_mesh_cpu_rewrite {

struct Args {
    const runtime::render_model::MeshData* mesh = nullptr;
    const shared_projected_unit_backend_mesh_support::FastTexturedBatchTemplate* srcBatch =
        nullptr;
    int resolvedTriNodeIndex = -1;
    bool needsLitNormals = false;
    bool needsTangents = false;
    std::uint32_t itemIndex = 0u;
    std::uint64_t poseHash = 0ull;
    bool canCacheCpuRewrite = false;

    shared_projected_unit_backend_mesh_persistent::SyncContext persistentSync{};
    shared_projected_unit_backend_mesh_transforms::Resolver* transforms = nullptr;
    shared_world_batches::WorldIndexedBatch* dstBatch = nullptr;
};

void buildOrReuseCpuRewriteVertices(const Args& args);

} // namespace game::runtime::shared_projected_unit_backend_mesh_cpu_rewrite

