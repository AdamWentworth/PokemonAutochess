#pragma once

#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "vfx/effects/shared/SharedAuthoredBatchVFX.h"

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

class IRenderBackend;

namespace game::runtime {
namespace render_model {
struct MeshData;
}
}

namespace game::runtime::shared_authored_vfx_prewarm {

struct Args {
    IRenderBackend* renderer = nullptr;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath = nullptr;
    std::function<render_model::MeshData*(const std::string&)> ensureBackendMeshLoaded;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)> ensureBackendTextureLoaded;
};

struct Stats {
    std::size_t drawPasses = 0u;
    std::size_t bakedTextures = 0u;
    std::size_t warmedBatches = 0u;
};

Stats prewarmSnapshot(const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
                      const glm::vec3& cameraWorldPos,
                      const Args& args);

} // namespace game::runtime::shared_authored_vfx_prewarm
