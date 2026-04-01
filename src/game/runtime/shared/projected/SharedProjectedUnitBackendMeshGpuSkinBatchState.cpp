#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshGpuSkinBatchState.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace support = game::runtime::shared_projected_unit_backend_mesh_support;

namespace game::runtime::shared_projected_unit_backend_mesh_gpu_skin_batch_state {

namespace {

bool matchesBatchStateKey(const support::UnitSkinMatrixKey& key,
                          int unitId,
                          int skinCacheKey,
                          const std::vector<std::uint16_t>* jointPalette,
                          std::size_t paletteCount) {
    if (key.unitId != unitId ||
        key.skinKey != skinCacheKey ||
        key.paletteSize != static_cast<std::uint32_t>(paletteCount)) {
        return false;
    }
    if (paletteCount == 0u || !jointPalette) {
        return true;
    }
    return std::memcmp(
               key.palette.data(),
               jointPalette->data(),
               paletteCount * sizeof(std::uint16_t)) == 0;
}

} // namespace

support::GpuSkinBatchStateEntry* resolveGpuSkinBatchState(
    std::vector<support::GpuSkinBatchStateEntry>& gpuSkinBatchStates,
    support::GpuSkinBatchStateEntry*& lastGpuSkinBatchState,
    shared_projected_unit_backend_mesh_transforms::Resolver& transforms,
    int unitId,
    int resolvedTriNodeIndex,
    int skinCacheKey,
    const std::vector<std::uint16_t>* jointPalette) {
    const bool hasJointPalette = jointPalette && !jointPalette->empty();
    const std::size_t paletteCount =
        hasJointPalette ? std::min(jointPalette->size(), support::kMaxGpuSkinMatrices) : 0u;

    support::GpuSkinBatchStateEntry* matchedEntry = nullptr;
    if (lastGpuSkinBatchState &&
        matchesBatchStateKey(
            lastGpuSkinBatchState->key,
            unitId,
            skinCacheKey,
            jointPalette,
            paletteCount)) {
        matchedEntry = lastGpuSkinBatchState;
    } else {
        auto stateIt = std::find_if(
            gpuSkinBatchStates.begin(),
            gpuSkinBatchStates.end(),
            [&](const support::GpuSkinBatchStateEntry& entry) {
                return matchesBatchStateKey(
                    entry.key,
                    unitId,
                    skinCacheKey,
                    jointPalette,
                    paletteCount);
            });
        if (stateIt != gpuSkinBatchStates.end()) {
            matchedEntry = &(*stateIt);
        }
    }

    if (!matchedEntry) {
        support::UnitSkinMatrixKey batchStateKey{};
        batchStateKey.unitId = unitId;
        batchStateKey.skinKey = skinCacheKey;
        batchStateKey.paletteSize = static_cast<std::uint32_t>(paletteCount);
        if (paletteCount > 0u && jointPalette) {
            std::memcpy(batchStateKey.palette.data(),
                        jointPalette->data(),
                        paletteCount * sizeof(std::uint16_t));
        }

        support::GpuSkinBatchStateEntry newEntry{};
        newEntry.key = batchStateKey;
        auto& sharedSkinMatrices = support::unitSkinMatrices()[batchStateKey];
        if (transforms.configureGpuClipSkinningBatch(
                resolvedTriNodeIndex,
                hasJointPalette ? jointPalette : nullptr,
                newEntry.state.modelMatrix,
                newEntry.state.gpuSkinningMode,
                sharedSkinMatrices,
                newEntry.state.skinMatrixCount)) {
            newEntry.state.valid = true;
            newEntry.state.sharedSkinMatrices =
                sharedSkinMatrices.empty() ? nullptr : sharedSkinMatrices.data();
        }

        gpuSkinBatchStates.emplace_back(std::move(newEntry));
        matchedEntry = &gpuSkinBatchStates.back();
    }

    lastGpuSkinBatchState = matchedEntry;
    return matchedEntry;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_gpu_skin_batch_state
