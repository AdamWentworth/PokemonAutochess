#include "game/runtime/shared/vfx/tail_fire/SharedTailFireExactCpuSnapshotBatches.h"

#include "game/runtime/shared/vfx/tail_fire/SharedTailFireExactCpuTileBake.h"

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
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches) {
    return game::runtime::shared_tail_fire_exact_cpu_tile_bake::appendExactBatchWithTileBake(
        label,
        snapshot,
        viewProj,
        invViewProj,
        cameraWorldPos,
        drawableW,
        drawableH,
        blendMode,
        primaryRawTex,
        secondaryRawTex,
        worldIndexedBatches);
}

} // namespace game::runtime::shared_tail_fire_exact_cpu_snapshot

