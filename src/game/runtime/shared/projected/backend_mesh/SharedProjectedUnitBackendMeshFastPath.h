#pragma once

#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPersistentItems.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTransforms.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_unit_backend_mesh_fast_path {

struct DirectFastTexturedArgs {
    const PokemonInstance* unit = nullptr;
    const shared_projected_unit_backend_mesh_prep::PreparedState* prep = nullptr;
    shared_projected_unit_backend_mesh_transforms::Resolver* transforms = nullptr;
    const std::vector<glm::mat4>* nodeGlobals = nullptr;
    const shared_projected_unit_backend_mesh_support::FastTexturedMeshTemplateCache* fastCache =
        nullptr;

    float fastTexturedAlpha = 1.0f;
    glm::vec3 fastTexturedTint{1.0f};
    glm::vec3 cameraWorldPos{0.0f};
    glm::vec3 proxyCenter{0.0f};
    float modelFadeAlpha = 1.0f;
    bool enableGpuClipSkinning = false;

    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* modelIndexedBatchesPerSubmesh = nullptr;
    shared_projected_unit_backend_mesh_persistent::SyncContext persistentSync{};
};

struct DirectFastTexturedResult {
    bool handled = false;
    bool queuedIndexedBatch = false;
    std::uint32_t sharedRigidBatches = 0u;
    std::uint32_t gpuClipSkinBatches = 0u;
    std::uint32_t gpuClipPaletteBatches = 0u;
    std::uint32_t indexedBatchesQueued = 0u;
};

DirectFastTexturedResult tryQueueDirectFastTexturedWorldBatches(
    const DirectFastTexturedArgs& args);

} // namespace game::runtime::shared_projected_unit_backend_mesh_fast_path

