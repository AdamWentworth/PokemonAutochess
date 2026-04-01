#pragma once

#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCpuRewrite.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPersistentItems.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTransforms.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_unit_backend_mesh_cached_indexed_batches {

struct Args {
    const PokemonInstance* unit = nullptr;
    const runtime::render_model::MeshData* mesh = nullptr;
    const shared_projected_unit_backend_mesh_prep::PreparedState* prep = nullptr;
    shared_projected_unit_backend_mesh_transforms::Resolver* transforms = nullptr;
    const std::vector<glm::mat4>* nodeGlobals = nullptr;
    const shared_projected_unit_backend_mesh_support::FastTexturedMeshTemplateCache* fastCache =
        nullptr;

    float fastTexturedAlpha = 1.0f;
    glm::vec3 fastTexturedTint{1.0f};
    bool enableGpuClipSkinning = false;

    shared_projected_unit_backend_mesh_persistent::SyncContext persistentSync{};
    bool* cpuRewritePoseHashReady = nullptr;
    std::uint64_t* cpuRewritePoseHash = nullptr;

    std::vector<shared_world_batches::WorldIndexedBatch>* modelIndexedBatchesPerSubmesh = nullptr;
    std::vector<std::uint8_t>* batchUsesGpuClipPalette = nullptr;
};

struct Result {
    bool handled = false;
    std::uint32_t sharedRigidBatches = 0u;
    std::uint32_t gpuClipSkinBatches = 0u;
    std::uint32_t gpuClipPaletteBatches = 0u;
    std::uint32_t cpuRewriteBatches = 0u;
};

Result buildCachedIndexedBatches(const Args& args);

} // namespace game::runtime::shared_projected_unit_backend_mesh_cached_indexed_batches

