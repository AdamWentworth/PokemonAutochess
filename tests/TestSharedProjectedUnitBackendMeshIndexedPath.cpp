#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshIndexedPath.h"

#include <string>

bool test_shared_projected_unit_backend_mesh_indexed_path_contract(std::string& outFail) {
    namespace indexed = game::runtime::shared_projected_unit_backend_mesh_indexed_path;
    using game::runtime::shared_world_batches::WorldIndexedBatch;

    {
        std::vector<WorldIndexedBatch> batches(1);
        auto& batch = batches[0];
        batch.gpuSkinning = 1u;
        batch.skinMatrixCount = 4u;
        batch.sharedSkinMatrices = reinterpret_cast<const float*>(0x1);
        batch.skinMatrices = {1.0f, 2.0f};
        batch.sharedVertices = reinterpret_cast<const IRenderBackend::WorldMeshVertex*>(0x1);
        batch.sharedVertexCount = 3u;
        batch.sharedIndices = reinterpret_cast<const std::uint32_t*>(0x1);
        batch.sharedIndexCount = 6u;

        indexed::clearIndexedBatchDynamicState(batches);
        if (batch.gpuSkinning != 0u ||
            batch.skinMatrixCount != 0u ||
            batch.sharedSkinMatrices != nullptr ||
            !batch.skinMatrices.empty() ||
            batch.sharedVertices != nullptr ||
            batch.sharedVertexCount != 0u ||
            batch.sharedIndices != nullptr ||
            batch.sharedIndexCount != 0u) {
            outFail = "Indexed-path helpers should clear per-frame dynamic batch state without touching geometry buffers.";
            return false;
        }
    }

    {
        std::vector<WorldIndexedBatch> batches(1);
        auto& batch = batches[0];
        batch.geometryCacheKey = "cached";
        batch.vertices.resize(3u);
        batch.indices = {0u, 1u, 2u};

        indexed::invalidateIndexedBatches(batches);
        if (!batch.geometryCacheKey.empty() ||
            !batch.vertices.empty() ||
            !batch.indices.empty()) {
            outFail = "Indexed-path helpers should invalidate cached geometry when the fast path cannot be used.";
            return false;
        }
    }

    {
        std::vector<WorldIndexedBatch> staged(2);
        staged[0].vertices.resize(3u);
        staged[0].indices = {0u, 1u, 2u};
        std::vector<WorldIndexedBatch> queued;
        game::runtime::shared_projected_unit_backend_mesh::Args renderArgs{};
        game::runtime::render_model::MeshData mesh{};

        const bool queuedAny = indexed::queueIndexedWorldBatches(
            indexed::QueueIndexedWorldBatchesArgs{
                renderArgs,
                mesh,
                staged,
                queued});
        if (!queuedAny || queued.size() != 1u) {
            outFail = "Indexed-path helpers should queue only batches that still contain geometry.";
            return false;
        }
    }

    return true;
}
