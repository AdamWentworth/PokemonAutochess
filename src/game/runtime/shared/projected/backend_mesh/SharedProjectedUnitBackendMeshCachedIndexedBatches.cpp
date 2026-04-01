#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCachedIndexedBatches.h"

#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGpuSkinBatchState.h"

#include <algorithm>
#include <cstring>

#include <glm/gtc/type_ptr.hpp>

namespace cpu_rewrite = game::runtime::shared_projected_unit_backend_mesh_cpu_rewrite;
namespace gpu_skin_batch_state =
    game::runtime::shared_projected_unit_backend_mesh_gpu_skin_batch_state;
namespace mesh_persistent = game::runtime::shared_projected_unit_backend_mesh_persistent;
namespace support = game::runtime::shared_projected_unit_backend_mesh_support;

namespace game::runtime::shared_projected_unit_backend_mesh_cached_indexed_batches {

namespace {

void clearIndexedBatchGeometry(shared_world_batches::WorldIndexedBatch& batch) {
    batch.vertices.clear();
    batch.indices.clear();
    batch.geometryCacheKey.clear();
    batch.sharedVertices = nullptr;
    batch.sharedVertexCount = 0u;
    batch.sharedIndices = nullptr;
    batch.sharedIndexCount = 0u;
    batch.gpuSkinning = 0u;
    batch.gpuSkinningMode = 0u;
    batch.skinMatrixCount = 0u;
    batch.sharedSkinMatrices = nullptr;
    batch.skinMatrices.clear();
}

void resetIndexedBatchSkinningState(shared_world_batches::WorldIndexedBatch& batch) {
    batch.gpuSkinning = 0u;
    batch.gpuSkinningMode = 0u;
    batch.skinMatrixCount = 0u;
    batch.sharedSkinMatrices = nullptr;
    batch.skinMatrices.clear();
    batch.sharedVertices = nullptr;
    batch.sharedVertexCount = 0u;
    batch.sharedIndices = nullptr;
    batch.sharedIndexCount = 0u;
}

bool anyIndexedBatchHasGeometry(
    const std::vector<shared_world_batches::WorldIndexedBatch>& batches) {
    for (const auto& batch : batches) {
        if (batch.hasGeometry()) {
            return true;
        }
    }
    return false;
}

} // namespace

Result buildCachedIndexedBatches(const Args& args) {
    Result out{};
    if (!args.unit || !args.mesh || !args.prep || !args.transforms || !args.nodeGlobals ||
        !args.fastCache || !args.modelIndexedBatchesPerSubmesh || !args.batchUsesGpuClipPalette) {
        return out;
    }

    const auto& prep = *args.prep;
    const auto& fastCache = *args.fastCache;
    const auto& nodeGlobals = *args.nodeGlobals;
    auto& modelIndexedBatchesPerSubmesh = *args.modelIndexedBatchesPerSubmesh;
    auto& batchUsesGpuClipPalette = *args.batchUsesGpuClipPalette;

    const std::size_t initialBatchCount = modelIndexedBatchesPerSubmesh.size();
    if (fastCache.batches.size() > initialBatchCount) {
        for (std::size_t bi = initialBatchCount; bi < fastCache.batches.size(); ++bi) {
            const std::size_t sourceBatchIndex = fastCache.batches[bi].baseSubmeshIndex;
            if (sourceBatchIndex >= initialBatchCount) {
                continue;
            }
            const auto& templateBatch = modelIndexedBatchesPerSubmesh[sourceBatchIndex];
            modelIndexedBatchesPerSubmesh.push_back(templateBatch);
            auto& newBatch = modelIndexedBatchesPerSubmesh.back();
            newBatch.geometryCacheKey = fastCache.batches[bi].geometryCacheKey;
            newBatch.vertices.clear();
            newBatch.indices.clear();
            newBatch.vertices.reserve(fastCache.batches[bi].sourceVertexIndices.size());
            newBatch.indices.reserve(fastCache.batches[bi].indices.size());
            newBatch.sharedVertices = nullptr;
            newBatch.sharedVertexCount = 0u;
            newBatch.sharedIndices = nullptr;
            newBatch.sharedIndexCount = 0u;
            newBatch.gpuSkinning = 0u;
            newBatch.gpuSkinningMode = 0u;
            newBatch.skinMatrixCount = 0u;
            newBatch.sharedSkinMatrices = nullptr;
            newBatch.skinMatrices.clear();
        }
    }
    batchUsesGpuClipPalette.assign(modelIndexedBatchesPerSubmesh.size(), 0u);

    for (auto& batch : modelIndexedBatchesPerSubmesh) {
        resetIndexedBatchSkinningState(batch);
    }

    auto resolveCpuRewritePoseHash = [&]() -> std::uint64_t {
        if (!args.cpuRewritePoseHashReady || !args.cpuRewritePoseHash) {
            return 0ull;
        }
        if (!*args.cpuRewritePoseHashReady) {
            *args.cpuRewritePoseHash = mesh_persistent::hashScenePoseEval(prep.scenePose);
            *args.cpuRewritePoseHashReady = true;
        }
        return *args.cpuRewritePoseHash;
    };

    auto& gpuSkinBatchStates = support::gpuSkinBatchStateEntries();
    gpuSkinBatchStates.clear();
    gpuSkinBatchStates.reserve(fastCache.batches.size());
    support::GpuSkinBatchStateEntry* lastGpuSkinBatchState = nullptr;

    for (std::size_t bi = 0; bi < fastCache.batches.size(); ++bi) {
        if (bi >= modelIndexedBatchesPerSubmesh.size()) {
            continue;
        }

        const auto& srcBatch = fastCache.batches[bi];
        auto& dstBatch = modelIndexedBatchesPerSubmesh[bi];
        dstBatch.vertexColorMulR = args.fastTexturedTint.r;
        dstBatch.vertexColorMulG = args.fastTexturedTint.g;
        dstBatch.vertexColorMulB = args.fastTexturedTint.b;
        dstBatch.vertexColorMulA = args.fastTexturedAlpha;
        int resolvedTriNodeIndex = srcBatch.triNodeIndex;
        if (resolvedTriNodeIndex < 0 && fastCache.defaultSkinNodeIndex >= 0) {
            resolvedTriNodeIndex = fastCache.defaultSkinNodeIndex;
        }

        if (args.enableGpuClipSkinning) {
            const int skinCacheKey = args.transforms->gpuSkinningCacheKeyForNode(
                resolvedTriNodeIndex);
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
                if (matchedEntry->state.valid) {
                    dstBatch.gpuSkinning = 1u;
                    dstBatch.gpuSkinningMode = matchedEntry->state.gpuSkinningMode;
                    dstBatch.modelMatrix = matchedEntry->state.modelMatrix;
                    dstBatch.skinMatrixCount = matchedEntry->state.skinMatrixCount;
                    dstBatch.sharedSkinMatrices = matchedEntry->state.sharedSkinMatrices;
                    dstBatch.skinMatrices.clear();
                    if (!srcBatch.gpuJointPalette.empty() && bi < batchUsesGpuClipPalette.size()) {
                        batchUsesGpuClipPalette[bi] = 1u;
                    }
                }
            }
        }

        if (dstBatch.gpuSkinning != 0u) {
            if (!srcBatch.gpuTemplateVertices.empty() && !srcBatch.indices.empty()) {
                dstBatch.vertices.clear();
                dstBatch.indices.clear();
                dstBatch.sharedVertices = srcBatch.gpuTemplateVertices.data();
                dstBatch.sharedVertexCount = srcBatch.gpuTemplateVertices.size();
                dstBatch.sharedIndices = srcBatch.indices.data();
                dstBatch.sharedIndexCount = srcBatch.indices.size();
            } else {
                dstBatch.geometryCacheKey.clear();
                dstBatch.sharedVertices = nullptr;
                dstBatch.sharedVertexCount = 0u;
                dstBatch.indices.clear();
                if (!srcBatch.indices.empty()) {
                    dstBatch.sharedIndices = srcBatch.indices.data();
                    dstBatch.sharedIndexCount = srcBatch.indices.size();
                } else {
                    dstBatch.sharedIndices = nullptr;
                    dstBatch.sharedIndexCount = 0u;
                }
                dstBatch.vertices.resize(srcBatch.gpuTemplateVertices.size());
                if (!srcBatch.gpuTemplateVertices.empty()) {
                    std::memcpy(
                        dstBatch.vertices.data(),
                        srcBatch.gpuTemplateVertices.data(),
                        srcBatch.gpuTemplateVertices.size() *
                            sizeof(IRenderBackend::WorldMeshVertex));
                }
            }
        } else {
            dstBatch.gpuSkinningMode = 0u;
            dstBatch.sharedVertices = nullptr;
            dstBatch.sharedVertexCount = 0u;
            dstBatch.sharedSkinMatrices = nullptr;
            const auto& materialBatch = shared_world_batches::resolvedMaterialBatch(dstBatch);
            const bool needsLitNormals = materialBatch.materialMode >= 2u;
            const bool hasNormalTexture =
                shared_world_batches::resolvedHasNormalTexture(dstBatch);
            const bool needsTangents = needsLitNormals && hasNormalTexture;
            const bool canUseSharedNodeTransform =
                !srcBatch.skinnedBatch &&
                !srcBatch.gpuTemplateVertices.empty() &&
                !srcBatch.indices.empty();
            if (canUseSharedNodeTransform) {
                dstBatch.vertices.clear();
                dstBatch.indices.clear();
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
            } else {
                const bool canCacheCpuRewrite = prep.scenePose && prep.scenePose->hasScenePose;
                cpu_rewrite::buildOrReuseCpuRewriteVertices(
                    {
                        .mesh = args.mesh,
                        .srcBatch = &srcBatch,
                        .resolvedTriNodeIndex = resolvedTriNodeIndex,
                        .needsLitNormals = needsLitNormals,
                        .needsTangents = needsTangents,
                        .itemIndex = static_cast<std::uint32_t>(bi),
                        .poseHash = canCacheCpuRewrite ? resolveCpuRewritePoseHash() : 0ull,
                        .canCacheCpuRewrite = canCacheCpuRewrite,
                        .persistentSync = args.persistentSync,
                        .transforms = args.transforms,
                        .dstBatch = &dstBatch,
                    });
            }
        }
    }

    out.handled = anyIndexedBatchHasGeometry(modelIndexedBatchesPerSubmesh);
    if (!out.handled) {
        batchUsesGpuClipPalette.clear();
        for (auto& batch : modelIndexedBatchesPerSubmesh) {
            clearIndexedBatchGeometry(batch);
        }
        return out;
    }

    for (std::size_t bi = 0; bi < modelIndexedBatchesPerSubmesh.size(); ++bi) {
        const auto& batch = modelIndexedBatchesPerSubmesh[bi];
        if (!batch.hasGeometry()) {
            continue;
        }
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
    }

    return out;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_cached_indexed_batches

