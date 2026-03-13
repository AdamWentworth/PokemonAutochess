#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace game::runtime::session_tail_fire_prewarm {

struct Args {
    IRenderBackend* renderer = nullptr;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath = nullptr;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)> ensureBackendTextureLoaded;
};

startup_asset_prewarm::TailFireStats prewarm(const Args& args);

} // namespace game::runtime::session_tail_fire_prewarm
