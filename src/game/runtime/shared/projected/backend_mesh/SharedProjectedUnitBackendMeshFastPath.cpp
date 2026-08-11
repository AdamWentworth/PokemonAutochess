#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshFastPath.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGpuSkinBatchState.h"

#include <utility>

#include <glm/gtc/type_ptr.hpp>

namespace gpu_skin_batch_state =
    game::runtime::shared_projected_unit_backend_mesh_gpu_skin_batch_state;
namespace mesh_persistent = game::runtime::shared_projected_unit_backend_mesh_persistent;
namespace support = game::runtime::shared_projected_unit_backend_mesh_support;

namespace game::runtime::shared_projected_unit_backend_mesh_fast_path {

DirectFastTexturedResult tryQueueDirectFastTexturedWorldBatches(
    const DirectFastTexturedArgs& args) {
    DirectFastTexturedResult out{};
    if (!args.unit || !args.prep || !args.transforms || !args.nodeGlobals || !args.fastCache ||
        !args.worldIndexedBatches || !args.modelIndexedBatchesPerSubmesh) {
        return out;
    }

    const auto& prep = *args.prep;
    const auto& fastCache = *args.fastCache;
    const auto& nodeGlobals = *args.nodeGlobals;
    auto& worldIndexedBatches = *args.worldIndexedBatches;
    auto& modelIndexedBatchesPerSubmesh = *args.modelIndexedBatchesPerSubmesh;

    bool directFastPathHasGeometry = false;
    bool directFastPathFallbackNeeded = false;
    const std::size_t directBatchStart = worldIndexedBatches.size();
    const float batchSortDepth =
        glm::dot(args.cameraWorldPos - args.proxyCenter,
                 args.cameraWorldPos - args.proxyCenter);

    auto rollbackDirectFastBatches = [&]() {
        worldIndexedBatches.resize(directBatchStart);
    };

    auto& gpuSkinBatchStates = support::gpuSkinBatchStateEntries();
    gpuSkinBatchStates.clear();
    gpuSkinBatchStates.reserve(fastCache.batches.size());
    support::GpuSkinBatchStateEntry* lastGpuSkinBatchState = nullptr;

    for (std::size_t fastBatchIndex = 0; fastBatchIndex < fastCache.batches.size();
         ++fastBatchIndex) {
        const auto& srcBatch = fastCache.batches[fastBatchIndex];
        const bool hasSharedTemplate =
            srcBatch.baseSubmeshIndex < modelIndexedBatchesPerSubmesh.size() &&
            modelIndexedBatchesPerSubmesh[srcBatch.baseSubmeshIndex].sharedTemplate != nullptr;
        if (!hasSharedTemplate) {
            directFastPathFallbackNeeded = true;
            break;
        }

        worldIndexedBatches.emplace_back();
        auto& dstBatch = worldIndexedBatches.back();
        dstBatch.sharedTemplate =
            modelIndexedBatchesPerSubmesh[srcBatch.baseSubmeshIndex].sharedTemplate;
        dstBatch.geometryCacheKey = srcBatch.geometryCacheKey;
        dstBatch.vertexColorMulR = args.fastTexturedTint.r;
        dstBatch.vertexColorMulG = args.fastTexturedTint.g;
        dstBatch.vertexColorMulB = args.fastTexturedTint.b;
        const float visibilityAlpha =
            srcBatch.baseSubmeshIndex <
                    prep.submeshVisibilityAlpha.size()
                ? prep.submeshVisibilityAlpha[
                      srcBatch.baseSubmeshIndex]
                : 1.0f;
        dstBatch.vertexColorMulA =
            args.fastTexturedAlpha * visibilityAlpha;
        dstBatch.sortDepth = batchSortDepth;
        if (args.modelFadeAlpha < 0.999f) {
            dstBatch.materialAlphaOverride = true;
            dstBatch.alphaMode = 2u;
            dstBatch.blendMode = 0u;
            dstBatch.alphaCutoff = 0.0f;
        }

        int resolvedTriNodeIndex = srcBatch.triNodeIndex;
        if (resolvedTriNodeIndex < 0 && fastCache.defaultSkinNodeIndex >= 0) {
            resolvedTriNodeIndex = fastCache.defaultSkinNodeIndex;
        }

        bool configuredBatch = false;
        bool canUseSharedNodeTransform = false;
        if (args.enableGpuClipSkinning) {
            const int skinCacheKey =
                args.transforms->gpuSkinningCacheKeyForNode(resolvedTriNodeIndex);
            if (skinCacheKey >= 0) {
                support::GpuSkinBatchStateEntry* matchedEntry =
                    gpu_skin_batch_state::resolveGpuSkinBatchState(
                        gpuSkinBatchStates,
                        lastGpuSkinBatchState,
                        *args.transforms,
                        args.unit->id,
                        resolvedTriNodeIndex,
                        skinCacheKey,
                        srcBatch.gpuJointPalette.empty() ? nullptr : &srcBatch.gpuJointPalette);
                if (matchedEntry->state.valid &&
                    !srcBatch.gpuTemplateVertices.empty() &&
                    !srcBatch.indices.empty()) {
                    dstBatch.gpuSkinning = 1u;
                    dstBatch.gpuSkinningMode = matchedEntry->state.gpuSkinningMode;
                    dstBatch.modelMatrix = matchedEntry->state.modelMatrix;
                    dstBatch.skinMatrixCount = matchedEntry->state.skinMatrixCount;
                    dstBatch.sharedSkinMatrices = matchedEntry->state.sharedSkinMatrices;
                    dstBatch.sharedVertices = srcBatch.gpuTemplateVertices.data();
                    dstBatch.sharedVertexCount = srcBatch.gpuTemplateVertices.size();
                    dstBatch.sharedIndices = srcBatch.indices.data();
                    dstBatch.sharedIndexCount = srcBatch.indices.size();
                    directFastPathHasGeometry = true;
                    configuredBatch = true;
                    ++out.gpuClipSkinBatches;
                    if (!srcBatch.gpuJointPalette.empty()) {
                        ++out.gpuClipPaletteBatches;
                    }
                }
            }
        }

        if (!configuredBatch) {
            canUseSharedNodeTransform =
                !srcBatch.skinnedBatch &&
                !srcBatch.gpuTemplateVertices.empty() &&
                !srcBatch.indices.empty();
            if (canUseSharedNodeTransform) {
                dstBatch.sharedVertices = srcBatch.gpuTemplateVertices.data();
                dstBatch.sharedVertexCount = srcBatch.gpuTemplateVertices.size();
                dstBatch.sharedIndices = srcBatch.indices.data();
                dstBatch.sharedIndexCount = srcBatch.indices.size();

                glm::mat4 nodeGlobal(1.0f);
                if (resolvedTriNodeIndex >= 0 &&
                    static_cast<std::size_t>(resolvedTriNodeIndex) < nodeGlobals.size()) {
                    nodeGlobal = nodeGlobals[static_cast<std::size_t>(resolvedTriNodeIndex)];
                }
                const glm::mat4 batchModel = prep.modelM * nodeGlobal;
                const float* batchModelData = glm::value_ptr(batchModel);
                std::copy(batchModelData, batchModelData + 16, dstBatch.modelMatrix.begin());
                directFastPathHasGeometry = true;
                configuredBatch = true;
                ++out.sharedRigidBatches;
            }
        }

        if (!configuredBatch) {
            directFastPathFallbackNeeded = true;
            break;
        }

        const bool hasStableGpuTemplate =
            !srcBatch.gpuTemplateVertices.empty() && !srcBatch.indices.empty();
        mesh_persistent::syncPersistentRenderItem(
            args.persistentSync,
            static_cast<std::uint32_t>(fastBatchIndex),
            dstBatch,
            static_cast<std::uint32_t>(srcBatch.baseSubmeshIndex),
            resolvedTriNodeIndex,
            resolvedTriNodeIndex,
            srcBatch.skinnedBatch,
            canUseSharedNodeTransform,
            hasStableGpuTemplate,
            hasStableGpuTemplate ? static_cast<const void*>(&srcBatch)
                                 : nullptr);

        ++out.indexedBatchesQueued;
    }

    if (directFastPathFallbackNeeded || !directFastPathHasGeometry) {
        rollbackDirectFastBatches();
        return out;
    }

    out.handled = true;
    out.queuedIndexedBatch = out.indexedBatchesQueued > 0u;
    return out;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_fast_path

