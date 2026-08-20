#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

namespace game::runtime::shared_particle_snapshot_billboards {

std::string makeSharedParticleTextureCacheKey(const std::string& texturePath);
const std::vector<std::string>& commonParticleTexturePaths();

bool appendSnapshotAsBillboards(
    const char* label,
    const ParticleSystem::RenderSnapshot& snapshot,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches);

} // namespace game::runtime::shared_particle_snapshot_billboards
