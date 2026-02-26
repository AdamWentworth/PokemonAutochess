#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"
#include "game/runtime/shared/SharedBackendTextureCache.h"
#include "game/runtime/shared/SharedWorldIndexedBatches.h"

namespace game::runtime::shared_tail_fire_snapshot_billboards {

struct AppendContext;

struct TailFireCombinedAtlasInfo {
    SharedBackendTextureCacheEntry* atlas = nullptr;
    std::string cacheKey;
    glm::vec4 rect0{0.0f, 0.0f, 1.0f, 1.0f};
    glm::vec4 rect1{0.0f, 0.0f, 1.0f, 1.0f};
    bool hasSecondary = false;
};

SharedBackendTextureCacheEntry* resolveTailFirePremulAtlas(
    const std::string& atlasPath,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureTextureFn);

TailFireCombinedAtlasInfo resolveTailFireCombinedAtlas(
    const ParticleSystem::RenderSnapshot& snapshot,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureTextureFn);

bool appendTailFireExactGpuBatch(
    const char* label,
    const ParticleSystem::RenderSnapshot& snapshot,
    std::uint8_t blendMode,
    const AppendContext& ctx,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches);

} // namespace game::runtime::shared_tail_fire_snapshot_billboards
