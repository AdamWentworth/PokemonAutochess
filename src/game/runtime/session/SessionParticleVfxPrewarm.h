#pragma once

#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"

#include <functional>
#include <string>

class IRenderBackend;

namespace game::runtime::session_particle_vfx_prewarm {

struct Args {
    IRenderBackend* renderer = nullptr;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)> ensureBackendTextureLoaded;
};

startup_asset_prewarm::ParticleVfxStats prewarm(const Args& args);

} // namespace game::runtime::session_particle_vfx_prewarm
