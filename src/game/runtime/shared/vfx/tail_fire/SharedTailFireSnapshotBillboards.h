#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

namespace game::runtime::shared_tail_fire_snapshot_billboards {

struct AppendContext {
    const glm::mat4& viewProj;
    const glm::mat4& invViewProj;
    const glm::vec3& cameraWorldPos;
    int drawableW;
    int drawableH;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath;
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureTextureFn;
    const std::unordered_map<int, shared_tail_fire_fallback::Anchor>* tailFireAnchors = nullptr;
    bool tailFireExactCpuEnabled = false;
    bool tailFireDebugEnabled = false;
};

bool appendTailFireSnapshotBillboards(
    const char* label,
    const ParticleSystem::RenderSnapshot& snapshot,
    std::uint8_t blendMode,
    const AppendContext& ctx,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches);

} // namespace game::runtime::shared_tail_fire_snapshot_billboards

