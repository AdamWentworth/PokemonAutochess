#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshIndexedFinalize.h"

namespace mesh_persistent = game::runtime::shared_projected_unit_backend_mesh_persistent;
namespace support = game::runtime::shared_projected_unit_backend_mesh_support;

namespace game::runtime::shared_projected_unit_backend_mesh_indexed_finalize {

Result finalizeIndexedWorldBatches(const Args& args) {
    Result out{};
    if (!args.renderArgs || !args.mesh || !args.submeshNodeFallback || !args.persistentSync ||
        !args.modelIndexedBatchesPerSubmesh || !args.batchUsesGpuClipPalette ||
        !args.worldIndexedBatches) {
        return out;
    }

    auto& modelIndexedBatchesPerSubmesh = *args.modelIndexedBatchesPerSubmesh;
    if (modelIndexedBatchesPerSubmesh.empty()) {
        return out;
    }

    auto& batchUsesGpuClipPalette = *args.batchUsesGpuClipPalette;
    auto& worldIndexedBatches = *args.worldIndexedBatches;
    const auto& submeshNodeFallback = *args.submeshNodeFallback;
    const auto* fastCache = args.fastCache;

    (void)support::applyTailFireMeshFlipbookOverride(
        *args.renderArgs,
        *args.mesh,
        modelIndexedBatchesPerSubmesh);

    for (std::size_t bi = 0; bi < modelIndexedBatchesPerSubmesh.size(); ++bi) {
        auto& batch = modelIndexedBatchesPerSubmesh[bi];
        if (!batch.hasGeometry()) {
            continue;
        }

        std::size_t baseSubmeshIndex = support::resolveBatchBaseSubmeshIndex(batch, bi);
        int triNodeIndex = -1;
        bool skinnedBatch = false;
        bool hasStableGpuTemplate =
            batch.sharedVertices != nullptr &&
            batch.sharedVertexCount > 0u &&
            batch.sharedIndices != nullptr &&
            batch.sharedIndexCount > 0u;
        if (fastCache && bi < fastCache->batches.size()) {
            const auto& srcBatch = fastCache->batches[bi];
            baseSubmeshIndex = srcBatch.baseSubmeshIndex;
            triNodeIndex = srcBatch.triNodeIndex;
            skinnedBatch = srcBatch.skinnedBatch;
            hasStableGpuTemplate =
                !srcBatch.gpuTemplateVertices.empty() && !srcBatch.indices.empty();
        } else if (baseSubmeshIndex < submeshNodeFallback.size()) {
            triNodeIndex = submeshNodeFallback[baseSubmeshIndex];
        }

        const bool canUseSharedNodeTransform =
            !skinnedBatch &&
            batch.gpuSkinning == 0u &&
            batch.sharedVertices != nullptr &&
            batch.sharedVertexCount > 0u &&
            batch.sharedIndices != nullptr &&
            batch.sharedIndexCount > 0u;
        mesh_persistent::syncPersistentRenderItem(
            *args.persistentSync,
            static_cast<std::uint32_t>(bi),
            batch,
            static_cast<std::uint32_t>(baseSubmeshIndex),
            triNodeIndex,
            triNodeIndex,
            skinnedBatch,
            canUseSharedNodeTransform,
            hasStableGpuTemplate,
            (fastCache && bi < fastCache->batches.size() && hasStableGpuTemplate)
                ? static_cast<const void*>(&fastCache->batches[bi])
                : nullptr);

        if (batch.gpuSkinning != 0u) {
            ++out.gpuClipSkinBatches;
            if (bi < batchUsesGpuClipPalette.size() && batchUsesGpuClipPalette[bi] != 0u) {
                ++out.gpuClipPaletteBatches;
            }
        } else if (batch.sharedVertices != nullptr &&
                   batch.sharedVertexCount > 0u &&
                   batch.sharedIndices != nullptr &&
                   batch.sharedIndexCount > 0u) {
            ++out.sharedRigidBatches;
        } else {
            ++out.cpuRewriteBatches;
        }

        worldIndexedBatches.push_back(std::move(batch));
        out.queuedIndexedBatch = true;
        ++out.indexedBatchesQueued;
    }

    return out;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_indexed_finalize
