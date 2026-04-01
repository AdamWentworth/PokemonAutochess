#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTrianglePrep.h"

#include <algorithm>

#include <glm/gtc/type_ptr.hpp>

namespace support = game::runtime::shared_projected_unit_backend_mesh_support;

namespace game::runtime::shared_projected_unit_backend_mesh_triangle_prep {

void initializeIndexedTrianglePrep(const Args& args, State& state) {
    state.fastBatchUsesRigidNodeGpuSkin.clear();
    state.fastBatchRigidNodePaletteIndex.clear();
    state.fastBatchRigidVertexRemap.clear();

    if (!args.mesh || !args.submeshNodeFallback || !args.nodeGlobals ||
        !args.modelMatrix || !args.triNodeIndexByTriangle ||
        !args.modelIndexedBatchesPerSubmesh) {
        return;
    }

    const auto& mesh = *args.mesh;
    const auto& submeshNodeFallback = *args.submeshNodeFallback;
    const auto& nodeGlobals = *args.nodeGlobals;
    auto& triNodeIndexByTriangle = *args.triNodeIndexByTriangle;
    auto& modelIndexedBatchesPerSubmesh = *args.modelIndexedBatchesPerSubmesh;

    triNodeIndexByTriangle.assign(args.triangleCount, -1);
    for (std::size_t triIdx = 0; triIdx < args.triangleCount; ++triIdx) {
        int triNodeIndex =
            (triIdx < mesh.triangleNodeIndex.size()) ? mesh.triangleNodeIndex[triIdx] : -1;
        if (triNodeIndex < 0 &&
            triIdx < mesh.triangleSubmesh.size() &&
            !submeshNodeFallback.empty()) {
            const std::uint16_t submeshIndex = mesh.triangleSubmesh[triIdx];
            if (submeshIndex < submeshNodeFallback.size()) {
                triNodeIndex = submeshNodeFallback[submeshIndex];
            }
        }
        triNodeIndexByTriangle[triIdx] = triNodeIndex;
    }

    if (!args.useFastTexturedFullMeshPath || !args.enableGpuClipSkinning ||
        modelIndexedBatchesPerSubmesh.empty()) {
        return;
    }

    state.fastBatchUsesRigidNodeGpuSkin.assign(modelIndexedBatchesPerSubmesh.size(), 0u);
    std::vector<std::vector<int>> fastBatchRigidNodePaletteNodes(
        modelIndexedBatchesPerSubmesh.size());
    state.fastBatchRigidNodePaletteIndex.resize(modelIndexedBatchesPerSubmesh.size());
    state.fastBatchRigidVertexRemap.resize(modelIndexedBatchesPerSubmesh.size());
    std::vector<std::uint8_t> fastBatchRigidNodeOverflow(
        modelIndexedBatchesPerSubmesh.size(), 0u);

    for (std::size_t triIdx = 0; triIdx < args.triangleCount; ++triIdx) {
        const std::uint16_t triSubmeshIndex =
            (triIdx < mesh.triangleSubmesh.size())
                ? mesh.triangleSubmesh[triIdx]
                : static_cast<std::uint16_t>(0u);
        std::size_t batchIndex = static_cast<std::size_t>(triSubmeshIndex);
        if (batchIndex >= modelIndexedBatchesPerSubmesh.size()) {
            batchIndex = 0u;
        }
        if (fastBatchRigidNodeOverflow[batchIndex] != 0u) {
            continue;
        }

        const auto& batch = modelIndexedBatchesPerSubmesh[batchIndex];
        if (batch.gpuSkinning != 0u) {
            continue;
        }
        if (!shared_world_batches::resolvedHasBaseTexture(batch)) {
            continue;
        }

        const int triNodeIndex = triNodeIndexByTriangle[triIdx];
        if (triNodeIndex < 0 ||
            static_cast<std::size_t>(triNodeIndex) >= nodeGlobals.size()) {
            fastBatchRigidNodeOverflow[batchIndex] = 1u;
            fastBatchRigidNodePaletteNodes[batchIndex].clear();
            continue;
        }

        auto& nodePalette = fastBatchRigidNodePaletteNodes[batchIndex];
        const bool alreadyPresent =
            std::find(nodePalette.begin(), nodePalette.end(), triNodeIndex) != nodePalette.end();
        if (alreadyPresent) {
            continue;
        }
        if (nodePalette.size() >= support::kMaxGpuSkinMatrices) {
            fastBatchRigidNodeOverflow[batchIndex] = 1u;
            nodePalette.clear();
            continue;
        }
        nodePalette.push_back(triNodeIndex);
    }

    const float* modelMatrixData = glm::value_ptr(*args.modelMatrix);
    for (std::size_t batchIndex = 0u; batchIndex < modelIndexedBatchesPerSubmesh.size();
         ++batchIndex) {
        if (fastBatchRigidNodeOverflow[batchIndex] != 0u) {
            continue;
        }

        auto& batch = modelIndexedBatchesPerSubmesh[batchIndex];
        if (batch.gpuSkinning != 0u) {
            continue;
        }
        if (!shared_world_batches::resolvedHasBaseTexture(batch)) {
            continue;
        }

        const auto& nodePalette = fastBatchRigidNodePaletteNodes[batchIndex];
        if (nodePalette.empty()) {
            continue;
        }

        batch.gpuSkinning = 1u;
        batch.gpuSkinningMode = 0u;
        batch.sharedSkinMatrices = nullptr;
        batch.skinMatrixCount = static_cast<std::uint32_t>(nodePalette.size());
        batch.skinMatrices.resize(nodePalette.size() * 16u);
        std::copy(modelMatrixData, modelMatrixData + 16, batch.modelMatrix.begin());

        auto& nodeToPaletteIndex = state.fastBatchRigidNodePaletteIndex[batchIndex];
        nodeToPaletteIndex.reserve(nodePalette.size());
        for (std::size_t paletteIndex = 0u; paletteIndex < nodePalette.size();
             ++paletteIndex) {
            const int nodeIndex = nodePalette[paletteIndex];
            nodeToPaletteIndex.emplace(nodeIndex, static_cast<std::uint16_t>(paletteIndex));
            const float* nodeGlobalData =
                glm::value_ptr(nodeGlobals[static_cast<std::size_t>(nodeIndex)]);
            std::copy(nodeGlobalData,
                      nodeGlobalData + 16,
                      batch.skinMatrices.data() + (paletteIndex * 16u));
        }
        state.fastBatchUsesRigidNodeGpuSkin[batchIndex] = 1u;
    }
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_triangle_prep

