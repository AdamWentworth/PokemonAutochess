#pragma once

#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"

#include <functional>
#include <string>
#include <unordered_map>

class IRenderBackend;

namespace game::runtime {
namespace render_model {
struct MeshData;
}
}

namespace game::runtime::tackle_vfx_prewarm {

struct Args {
    IRenderBackend* renderer = nullptr;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath = nullptr;
    std::function<render_model::MeshData*(const std::string&)> ensureBackendMeshLoaded;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)> ensureBackendTextureLoaded;
};

startup_asset_prewarm::TackleStats prewarm(const Args& args);

} // namespace game::runtime::tackle_vfx_prewarm
