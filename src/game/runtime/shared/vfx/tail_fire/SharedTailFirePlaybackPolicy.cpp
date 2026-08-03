#include "game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.h"

#include <cmath>

#include "game/runtime/shared/vfx/tail_fire/SharedTailFireCoordinator.h"

namespace game::runtime::shared_tail_fire_playback_policy {

bool usesAuthoredFireMeshMaterialFlags(float materialFlags) {
    const int fireFlags = static_cast<int>(std::lround(materialFlags));
    return (fireFlags & kAuthoredFireMeshFlagBit) != 0;
}

bool batchUsesAuthoredFireMesh(const shared_world_batches::WorldIndexedBatch& batch) {
    const auto& resolved = shared_world_batches::resolvedMaterialBatch(batch);
    return usesAuthoredFireMeshMaterialFlags(resolved.materialFlags);
}

bool hasAuthoredFireMeshBatches(
    const std::vector<shared_world_batches::WorldIndexedBatch>& batches) {
    for (const auto& batch : batches) {
        if (batchUsesAuthoredFireMesh(batch)) {
            return true;
        }
    }
    return false;
}

PlaybackMode resolvePlaybackMode(
    std::string_view species,
    const std::vector<shared_world_batches::WorldIndexedBatch>& batches) {
    if (!shared_tail_fire_coordinator::speciesUsesTailFireMeshPlayback(species)) {
        return PlaybackMode::None;
    }
    return hasAuthoredFireMeshBatches(batches)
        ? PlaybackMode::AuthoredMesh
        : PlaybackMode::SyntheticFallback;
}

bool shouldRenderSyntheticTailFireFallback(
    std::string_view species,
    const std::vector<shared_world_batches::WorldIndexedBatch>& batches) {
    return resolvePlaybackMode(species, batches) ==
           PlaybackMode::SyntheticFallback;
}

} // namespace game::runtime::shared_tail_fire_playback_policy
