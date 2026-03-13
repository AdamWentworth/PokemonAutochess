#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshIndexedPath.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include <glm/gtc/type_ptr.hpp>

namespace game::runtime::shared_projected_unit_backend_mesh_indexed_path {

namespace support = shared_projected_unit_backend_mesh_support;

void clearIndexedBatchDynamicState(
    std::vector<shared_world_batches::WorldIndexedBatch>& batches) {
    for (auto& batch : batches) {
        batch.gpuSkinning = 0u;
        batch.skinMatrixCount = 0u;
        batch.sharedSkinMatrices = nullptr;
        batch.skinMatrices.clear();
        batch.sharedVertices = nullptr;
        batch.sharedVertexCount = 0u;
        batch.sharedIndices = nullptr;
        batch.sharedIndexCount = 0u;
    }
}

void invalidateIndexedBatches(
    std::vector<shared_world_batches::WorldIndexedBatch>& batches) {
    clearIndexedBatchDynamicState(batches);
    for (auto& batch : batches) {
        batch.vertices.clear();
        batch.indices.clear();
        batch.geometryCacheKey.clear();
    }
}

bool applyFastTexturedPath(const FastTexturedPathArgs& args) {
    auto& modelIndexedBatchesPerSubmesh = args.prep.modelIndexedBatchesPerSubmesh;

    const std::size_t initialBatchCount = modelIndexedBatchesPerSubmesh.size();
    if (args.fastCache.batches.size() > initialBatchCount) {
        for (std::size_t bi = initialBatchCount; bi < args.fastCache.batches.size(); ++bi) {
            const std::size_t sourceBatchIndex = args.fastCache.batches[bi].baseSubmeshIndex;
            if (sourceBatchIndex >= initialBatchCount) continue;
            const auto& templateBatch = modelIndexedBatchesPerSubmesh[sourceBatchIndex];
            modelIndexedBatchesPerSubmesh.push_back(templateBatch);
            auto& newBatch = modelIndexedBatchesPerSubmesh.back();
            newBatch.geometryCacheKey = args.fastCache.batches[bi].geometryCacheKey;
            newBatch.vertices.clear();
            newBatch.indices.clear();
            newBatch.vertices.reserve(args.fastCache.batches[bi].sourceVertexIndices.size());
            newBatch.indices.reserve(args.fastCache.batches[bi].indices.size());
        }
    }

    clearIndexedBatchDynamicState(modelIndexedBatchesPerSubmesh);

    auto& gpuSkinBatchStates = support::gpuSkinBatchStateEntries();
    gpuSkinBatchStates.clear();
    if (gpuSkinBatchStates.capacity() < args.fastCache.batches.size()) {
        gpuSkinBatchStates.reserve(args.fastCache.batches.size());
    }

    for (std::size_t bi = 0; bi < args.fastCache.batches.size(); ++bi) {
        if (bi >= modelIndexedBatchesPerSubmesh.size()) continue;
        const auto& srcBatch = args.fastCache.batches[bi];
        auto& dstBatch = modelIndexedBatchesPerSubmesh[bi];
        dstBatch.vertexColorMulR = args.prep.fastTexturedTint.r;
        dstBatch.vertexColorMulG = args.prep.fastTexturedTint.g;
        dstBatch.vertexColorMulB = args.prep.fastTexturedTint.b;
        dstBatch.vertexColorMulA = args.prep.fastTexturedAlpha;
        int resolvedTriNodeIndex = srcBatch.triNodeIndex;
        if (resolvedTriNodeIndex < 0 && args.fastCache.defaultSkinNodeIndex >= 0) {
            resolvedTriNodeIndex = args.fastCache.defaultSkinNodeIndex;
        }

        if (args.renderArgs.enableGpuClipSkinning) {
            const int skinCacheKey = args.transforms.gpuSkinningCacheKeyForNode(
                resolvedTriNodeIndex);
            if (skinCacheKey >= 0) {
                const bool hasJointPalette = !srcBatch.gpuJointPalette.empty();
                support::UnitSkinMatrixKey batchStateKey{};
                batchStateKey.unitId = args.unit.id;
                batchStateKey.skinKey = skinCacheKey;
                if (hasJointPalette) {
                    const std::size_t paletteCount = std::min(
                        srcBatch.gpuJointPalette.size(),
                        support::kMaxGpuSkinMatrices);
                    batchStateKey.paletteSize =
                        static_cast<std::uint32_t>(paletteCount);
                    for (std::size_t pi = 0; pi < paletteCount; ++pi) {
                        batchStateKey.palette[pi] = srcBatch.gpuJointPalette[pi];
                    }
                }

                auto stateIt = std::find_if(
                    gpuSkinBatchStates.begin(),
                    gpuSkinBatchStates.end(),
                    [&](const support::GpuSkinBatchStateEntry& entry) {
                        return entry.key == batchStateKey;
                    });
                if (stateIt == gpuSkinBatchStates.end()) {
                    support::GpuSkinBatchState newState{};
                    auto& sharedSkinMatrices = support::unitSkinMatrices()[batchStateKey];
                    if (args.transforms.configureGpuClipSkinningBatch(
                            resolvedTriNodeIndex,
                            hasJointPalette ? &srcBatch.gpuJointPalette : nullptr,
                            newState.modelMatrix,
                            sharedSkinMatrices,
                            newState.skinMatrixCount)) {
                        newState.valid = true;
                        newState.sharedSkinMatrices = sharedSkinMatrices.data();
                    }
                    gpuSkinBatchStates.push_back(
                        support::GpuSkinBatchStateEntry{batchStateKey, newState});
                    stateIt = std::prev(gpuSkinBatchStates.end());
                }
                if (stateIt->state.valid) {
                    dstBatch.gpuSkinning = 1u;
                    dstBatch.modelMatrix = stateIt->state.modelMatrix;
                    dstBatch.skinMatrixCount = stateIt->state.skinMatrixCount;
                    dstBatch.sharedSkinMatrices = stateIt->state.sharedSkinMatrices;
                    dstBatch.skinMatrices.clear();
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
            continue;
        }

        dstBatch.sharedVertices = nullptr;
        dstBatch.sharedVertexCount = 0u;
        dstBatch.sharedSkinMatrices = nullptr;
        const auto& materialBatch =
            shared_world_batches::resolvedMaterialBatch(dstBatch);
        const bool needsLitNormals = materialBatch.materialMode >= 2u;
        const bool hasNormalTexture =
            shared_world_batches::resolvedHasNormalTexture(dstBatch);
        const bool needsTangents = needsLitNormals && hasNormalTexture;
        const bool canUseRigidNodeTransform =
            args.prep.scenePose.hasClipPose &&
            srcBatch.gpuJointPalette.empty() &&
            !srcBatch.gpuTemplateVertices.empty() &&
            !srcBatch.indices.empty();
        const bool canUseDynamicLocalPosNoSkin =
            !args.prep.scenePose.hasClipPose &&
            srcBatch.gpuJointPalette.empty() &&
            !srcBatch.gpuTemplateVertices.empty() &&
            !srcBatch.indices.empty();

        if (canUseRigidNodeTransform) {
            dstBatch.vertices.clear();
            dstBatch.indices.clear();
            dstBatch.sharedVertices = srcBatch.gpuTemplateVertices.data();
            dstBatch.sharedVertexCount = srcBatch.gpuTemplateVertices.size();
            dstBatch.sharedIndices = srcBatch.indices.data();
            dstBatch.sharedIndexCount = srcBatch.indices.size();

            glm::mat4 nodeGlobal(1.0f);
            if (resolvedTriNodeIndex >= 0 &&
                static_cast<std::size_t>(resolvedTriNodeIndex) < args.nodeGlobals.size()) {
                nodeGlobal = args.nodeGlobals[static_cast<std::size_t>(resolvedTriNodeIndex)];
            }
            const glm::mat4 batchModel = args.prep.modelM * nodeGlobal;
            const float* batchModelData = glm::value_ptr(batchModel);
            std::copy(batchModelData, batchModelData + 16, dstBatch.modelMatrix.begin());
        } else if (canUseDynamicLocalPosNoSkin) {
            dstBatch.geometryCacheKey.clear();
            dstBatch.indices.clear();
            dstBatch.sharedIndices = srcBatch.indices.data();
            dstBatch.sharedIndexCount = srcBatch.indices.size();
            dstBatch.vertices.resize(srcBatch.sourceVertexIndices.size());
            for (std::size_t vi = 0; vi < srcBatch.sourceVertexIndices.size(); ++vi) {
                const std::uint32_t srcIndex = srcBatch.sourceVertexIndices[vi];
                if (srcIndex >= args.mesh.vertices.size()) continue;
                const auto& srcVertex = args.mesh.vertices[srcIndex];
                IRenderBackend::WorldMeshVertex outVertex = srcBatch.gpuTemplateVertices[vi];
                const glm::vec3 localPos = args.transforms.resolveDeformedLocalVertexPos(
                    srcIndex, srcVertex);
                outVertex.x = localPos.x;
                outVertex.y = localPos.y;
                outVertex.z = localPos.z;
                dstBatch.vertices[vi] = outVertex;
            }

            glm::mat4 nodeGlobal(1.0f);
            if (resolvedTriNodeIndex >= 0 &&
                static_cast<std::size_t>(resolvedTriNodeIndex) < args.nodeGlobals.size()) {
                nodeGlobal = args.nodeGlobals[static_cast<std::size_t>(resolvedTriNodeIndex)];
            }
            const glm::mat4 batchModel = args.prep.modelM * nodeGlobal;
            const float* batchModelData = glm::value_ptr(batchModel);
            std::copy(batchModelData, batchModelData + 16, dstBatch.modelMatrix.begin());
        } else {
            dstBatch.geometryCacheKey.clear();
            dstBatch.indices.clear();
            if (!srcBatch.indices.empty()) {
                dstBatch.sharedIndices = srcBatch.indices.data();
                dstBatch.sharedIndexCount = srcBatch.indices.size();
            } else {
                dstBatch.sharedIndices = nullptr;
                dstBatch.sharedIndexCount = 0u;
            }
            dstBatch.vertices.resize(srcBatch.sourceVertexIndices.size());
            for (std::size_t vi = 0; vi < srcBatch.sourceVertexIndices.size(); ++vi) {
                const std::uint32_t srcIndex = srcBatch.sourceVertexIndices[vi];
                if (srcIndex >= args.mesh.vertices.size()) continue;
                const auto& srcVertex = args.mesh.vertices[srcIndex];

                IRenderBackend::WorldMeshVertex outVertex = srcBatch.gpuTemplateVertices[vi];
                const glm::vec3 pos = args.transforms.resolveWorldVertexPos(
                    resolvedTriNodeIndex, srcIndex, srcVertex);
                outVertex.x = pos.x;
                outVertex.y = pos.y;
                outVertex.z = pos.z;
                if (needsLitNormals) {
                    const glm::vec3 nrm = args.transforms.resolveModelVertexNormal(
                        resolvedTriNodeIndex, srcIndex, srcVertex);
                    outVertex.nx = nrm.x;
                    outVertex.ny = nrm.y;
                    outVertex.nz = nrm.z;
                }
                if (needsTangents) {
                    const glm::vec4 tan = args.transforms.resolveModelVertexTangent(
                        resolvedTriNodeIndex, srcIndex, srcVertex);
                    outVertex.tx = tan.x;
                    outVertex.ty = tan.y;
                    outVertex.tz = tan.z;
                    outVertex.tw = tan.w;
                }
                dstBatch.vertices[vi] = outVertex;
            }
        }
    }

    const bool fastPathHasGeometry = std::any_of(
        modelIndexedBatchesPerSubmesh.begin(),
        modelIndexedBatchesPerSubmesh.end(),
        [](const shared_world_batches::WorldIndexedBatch& batch) {
            return batch.hasGeometry();
        });
    if (!fastPathHasGeometry) {
        invalidateIndexedBatches(modelIndexedBatchesPerSubmesh);
        return false;
    }
    return true;
}

bool queueIndexedWorldBatches(const QueueIndexedWorldBatchesArgs& args) {
    (void)support::applyTailFireMeshFlipbookOverride(
        args.renderArgs,
        args.mesh,
        args.modelIndexedBatchesPerSubmesh);
    bool queuedIndexedBatch = false;
    for (auto& batch : args.modelIndexedBatchesPerSubmesh) {
        if (!batch.hasGeometry()) continue;
        args.worldIndexedBatches.push_back(std::move(batch));
        queuedIndexedBatch = true;
    }
    return queuedIndexedBatch;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_indexed_path
