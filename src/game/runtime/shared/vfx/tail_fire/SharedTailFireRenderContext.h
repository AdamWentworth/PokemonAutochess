#pragma once

#include "engine/vfx/ParticleSystem.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <functional>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

namespace game::runtime::shared_tail_fire_render {

struct RenderContext {
    const std::unordered_map<int, shared_tail_fire_fallback::Anchor>* anchors = nullptr;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)> ensureBackendTextureLoaded;
    bool useExactTailFireCpuPath = false;
};

bool appendSnapshotBillboards(
    const char* label,
    const ParticleSystem::RenderSnapshot& snapshot,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    const RenderContext& context);

} // namespace game::runtime::shared_tail_fire_render
