#pragma once

#include "game/runtime/shared/backend/SharedBackendTextureCache.h"

#include <string>
#include <unordered_map>

namespace game::runtime::session_texture_cache {

using TextureCache = std::unordered_map<std::string, SharedBackendTextureCacheEntry>;

SharedBackendTextureCacheEntry* ensureTextureLoaded(
    TextureCache& backendTextureByPath,
    const std::string& texturePath,
    bool flipVertical = false);

} // namespace game::runtime::session_texture_cache
