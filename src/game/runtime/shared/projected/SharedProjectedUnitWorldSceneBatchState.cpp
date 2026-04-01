#include "game/runtime/shared/projected/SharedProjectedUnitWorldSceneBatchState.h"

#include <algorithm>
#include <cstring>

namespace support = game::runtime::shared_projected_unit_backend_mesh_support;

namespace game::runtime::shared_projected_unit_world_scene::batch_state {
namespace {

bool batchNeedsSceneSkinning(const support::FastTexturedBatchTemplate& batchTemplate) {
    return batchTemplate.skinnedBatch || !batchTemplate.gpuJointPalette.empty();
}

} // namespace

void ResolvedBatchState::ensureTransformsInitialized(
    const game::runtime::shared_projected_unit_models::Args& args,
    const game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState& prepared) {
    if (!transformsInitialized) {
        transforms.initialize(args, prepared);
        transformsInitialized = true;
    }
}

bool resolveBatchState(
    const game::runtime::shared_projected_unit_models::Args& args,
    const game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState& prepared,
    const support::FastTexturedMeshTemplateCache& fastCache,
    const IRenderBackend::WorldSceneFastPathCaps& fastPathCaps,
    bool enableGpuClipSkinning,
    ResolvedBatchState& out) {
    out.batchSkinStates.assign(fastCache.batches.size(), support::GpuSkinBatchState{});
    out.batchUsesSceneSkinning.assign(fastCache.batches.size(), 0u);
    out.resolvedTriNodeIndices.assign(fastCache.batches.size(), -1);

    auto& gpuSkinBatchStates = support::gpuSkinBatchStateEntries();
    gpuSkinBatchStates.clear();
    gpuSkinBatchStates.reserve(fastCache.batches.size());
    support::GpuSkinBatchStateEntry* lastGpuSkinBatchState = nullptr;

    auto tryResolveSkinnedBatchState =
        [&](const support::FastTexturedBatchTemplate& batchTemplate,
            int resolvedTriNodeIndex,
            support::GpuSkinBatchState& outState) -> bool {
            if (!fastPathCaps.supportsSkinnedInstancing || !enableGpuClipSkinning) {
                return false;
            }

            out.ensureTransformsInitialized(args, prepared);
            const int skinCacheKey = out.transforms.gpuSkinningCacheKeyForNode(resolvedTriNodeIndex);
            if (skinCacheKey < 0) {
                return false;
            }

            const bool hasJointPalette = !batchTemplate.gpuJointPalette.empty();
            const std::size_t paletteCount = hasJointPalette
                ? std::min(batchTemplate.gpuJointPalette.size(), support::kMaxGpuSkinMatrices)
                : 0u;
            const auto matchesBatchStateKey =
                [&](const support::UnitSkinMatrixKey& key) {
                    if (key.unitId != args.unit->id ||
                        key.skinKey != skinCacheKey ||
                        key.paletteSize != static_cast<std::uint32_t>(paletteCount)) {
                        return false;
                    }
                    if (paletteCount == 0u) {
                        return true;
                    }
                    return std::memcmp(key.palette.data(),
                                       batchTemplate.gpuJointPalette.data(),
                                       paletteCount * sizeof(std::uint16_t)) == 0;
                };

            support::GpuSkinBatchStateEntry* matchedEntry = nullptr;
            if (lastGpuSkinBatchState && matchesBatchStateKey(lastGpuSkinBatchState->key)) {
                matchedEntry = lastGpuSkinBatchState;
            } else {
                auto stateIt = std::find_if(
                    gpuSkinBatchStates.begin(),
                    gpuSkinBatchStates.end(),
                    [&](const support::GpuSkinBatchStateEntry& entry) {
                        return matchesBatchStateKey(entry.key);
                    });
                if (stateIt != gpuSkinBatchStates.end()) {
                    matchedEntry = &(*stateIt);
                }
            }

            if (!matchedEntry) {
                support::UnitSkinMatrixKey batchStateKey{};
                batchStateKey.unitId = args.unit->id;
                batchStateKey.skinKey = skinCacheKey;
                batchStateKey.paletteSize = static_cast<std::uint32_t>(paletteCount);
                if (paletteCount > 0u) {
                    std::memcpy(batchStateKey.palette.data(),
                                batchTemplate.gpuJointPalette.data(),
                                paletteCount * sizeof(std::uint16_t));
                }

                support::GpuSkinBatchStateEntry newEntry{};
                newEntry.key = batchStateKey;
                auto& sharedSkinMatrices = support::unitSkinMatrices()[batchStateKey];
                if (out.transforms.configureGpuClipSkinningBatch(
                        resolvedTriNodeIndex,
                        hasJointPalette ? &batchTemplate.gpuJointPalette : nullptr,
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
            if (!matchedEntry->state.valid ||
                matchedEntry->state.sharedSkinMatrices == nullptr ||
                matchedEntry->state.skinMatrixCount == 0u) {
                return false;
            }

            outState = matchedEntry->state;
            return true;
        };

    for (std::size_t fastBatchIndex = 0; fastBatchIndex < fastCache.batches.size(); ++fastBatchIndex) {
        const auto& batchTemplate = fastCache.batches[fastBatchIndex];
        int resolvedTriNodeIndex = batchTemplate.triNodeIndex;
        if (resolvedTriNodeIndex < 0 && fastCache.defaultSkinNodeIndex >= 0) {
            resolvedTriNodeIndex = fastCache.defaultSkinNodeIndex;
        }
        out.resolvedTriNodeIndices[fastBatchIndex] = resolvedTriNodeIndex;

        const bool needsSceneSkinning = batchNeedsSceneSkinning(batchTemplate);
        if (!tryResolveSkinnedBatchState(
                batchTemplate,
                resolvedTriNodeIndex,
                out.batchSkinStates[fastBatchIndex])) {
            if (needsSceneSkinning) {
                return false;
            }
            continue;
        }
        out.batchUsesSceneSkinning[fastBatchIndex] = 1u;
    }

    return true;
}

} // namespace game::runtime::shared_projected_unit_world_scene::batch_state
