#pragma once

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "vfx/effects/shared/SharedAuthoredBatchVFX.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_authored_vfx_bridge {

struct Args {
    const SharedAuthoredBatchVFX::RenderSnapshot* snapshot = nullptr;
    glm::vec3 cameraWorldPos{0.0f};
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
    std::function<runtime::render_model::MeshData*(const std::string&)> ensureBackendMeshLoaded;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)> ensureBackendTextureLoaded;
    std::size_t reserveMultiplier = 4u;
};

bool appendSnapshot(const Args& args);

} // namespace game::runtime::shared_projected_authored_vfx_bridge
