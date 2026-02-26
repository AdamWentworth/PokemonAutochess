#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"
#include "game/runtime/shared/SharedBackendTextureCache.h"
#include "game/runtime/shared/SharedWorldIndexedBatches.h"

namespace game::runtime::shared_particle_snapshot_billboards {

bool appendSnapshotAsBillboards(
    const char* label,
    const ParticleSystem::RenderSnapshot& snapshot,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded,
    bool tailFireExactCpuEnabled,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches);

} // namespace game::runtime::shared_particle_snapshot_billboards
