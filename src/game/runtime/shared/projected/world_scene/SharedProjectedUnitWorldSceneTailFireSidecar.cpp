#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneTailFireSidecar.h"

#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneSubmission.h"

#include <algorithm>

namespace support = game::runtime::shared_projected_unit_backend_mesh_support;
namespace world_scene_submission =
    game::runtime::shared_projected_unit_world_scene::submission;

namespace game::runtime::shared_projected_unit_world_scene::tail_fire_sidecar {

bool buildTailFireSidecarBatches(
    const game::runtime::shared_projected_unit_models::Args& args,
    const game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState& prepared,
    const game::runtime::shared_projected_unit_backend_mesh_support::FastTexturedMeshTemplateCache&
        fastCache,
    const game::runtime::shared_tail_fire_mesh_playback::Profile& profile,
    const game::runtime::shared_projected_unit_world_scene::batch_state::ResolvedBatchState&
        batchState,
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>& outBatches) {
    outBatches.clear();
    if (!args.unit || !prepared.mesh || !args.ensureBackendTextureLoaded || !args.sharedTailFireAnchors) {
        return false;
    }

    outBatches.reserve(fastCache.batches.size());
    for (std::size_t fastBatchIndex = 0; fastBatchIndex < fastCache.batches.size();
         ++fastBatchIndex) {
        const auto& batchTemplate = fastCache.batches[fastBatchIndex];
        if (!world_scene_submission::batchUsesTailFireSubmesh(batchTemplate, &profile)) {
            continue;
        }
        if (batchTemplate.gpuTemplateVertices.empty() || batchTemplate.indices.size() < 3u) {
            return false;
        }

        outBatches.emplace_back();
        auto& batch = outBatches.back();
        batch.geometryCacheKey = batchTemplate.geometryCacheKey;
        batch.vertexColorMulR = prepared.fastTexturedTint.r;
        batch.vertexColorMulG = prepared.fastTexturedTint.g;
        batch.vertexColorMulB = prepared.fastTexturedTint.b;
        batch.vertexColorMulA = prepared.fastTexturedAlpha;
        batch.sortDepth = prepared.indexedBatchSortDepth;
        batch.sharedVertices = batchTemplate.gpuTemplateVertices.data();
        batch.sharedVertexCount = batchTemplate.gpuTemplateVertices.size();
        batch.sharedIndices = batchTemplate.indices.data();
        batch.sharedIndexCount = batchTemplate.indices.size();

        if (fastBatchIndex < batchState.batchUsesSceneSkinning.size() &&
            batchState.batchUsesSceneSkinning[fastBatchIndex] != 0u) {
            if (fastBatchIndex >= batchState.batchSkinStates.size()) {
                return false;
            }
            const auto& skinState = batchState.batchSkinStates[fastBatchIndex];
            if (!skinState.valid ||
                !skinState.sharedSkinMatrices ||
                skinState.skinMatrixCount == 0u) {
                return false;
            }
            batch.gpuSkinning = 1u;
            batch.gpuSkinningMode = skinState.gpuSkinningMode;
            batch.modelMatrix = skinState.modelMatrix;
            batch.skinMatrixCount = skinState.skinMatrixCount;
            batch.sharedSkinMatrices = skinState.sharedSkinMatrices;
        } else {
            batch.modelMatrix = world_scene_submission::buildRigidBatchModelMatrix(
                prepared,
                args.scenePose,
                fastBatchIndex < batchState.resolvedTriNodeIndices.size()
                    ? batchState.resolvedTriNodeIndices[fastBatchIndex]
                    : -1);
        }
    }

    if (outBatches.empty()) {
        return !profile.hasFireSubmesh;
    }

    if (!support::applyTailFireMeshFlipbookOverride(args, *prepared.mesh, outBatches)) {
        return false;
    }

    outBatches.erase(
        std::remove_if(
            outBatches.begin(),
            outBatches.end(),
            [](const game::runtime::shared_world_batches::WorldIndexedBatch& batch) {
                return !batch.hasGeometry();
            }),
        outBatches.end());
    return !profile.hasFireSubmesh || !outBatches.empty();
}

} // namespace game::runtime::shared_projected_unit_world_scene::tail_fire_sidecar

