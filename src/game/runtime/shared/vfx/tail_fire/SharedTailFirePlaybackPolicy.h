#pragma once

#include <string_view>
#include <vector>

#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

namespace game::runtime::shared_tail_fire_playback_policy {

inline constexpr int kAuthoredFireMeshFlagBit = 1 << 3;

bool usesAuthoredFireMeshMaterialFlags(float materialFlags);
bool batchUsesAuthoredFireMesh(const shared_world_batches::WorldIndexedBatch& batch);
bool hasAuthoredFireMeshBatches(
    const std::vector<shared_world_batches::WorldIndexedBatch>& batches);
bool shouldRenderSyntheticTailFireFallback(
    std::string_view species,
    const std::vector<shared_world_batches::WorldIndexedBatch>& batches);

} // namespace game::runtime::shared_tail_fire_playback_policy
