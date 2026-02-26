#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"
#include "game/runtime/SharedBackendTextureCache.h"
#include "game/runtime/SharedWorldIndexedBatches.h"

namespace game::runtime::shared_tail_fire_exact_cpu_snapshot {

bool appendExactBatch(
    const char* label,
    const ParticleSystem::RenderSnapshot& snapshot,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    std::uint8_t blendMode,
    const SharedBackendTextureCacheEntry* primaryRawTex,
    const SharedBackendTextureCacheEntry* secondaryRawTex,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches);

} // namespace game::runtime::shared_tail_fire_exact_cpu_snapshot
