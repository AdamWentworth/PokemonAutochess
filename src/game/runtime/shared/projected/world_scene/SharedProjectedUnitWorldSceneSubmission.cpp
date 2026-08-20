#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneSubmission.h"

#include "game/runtime/shared/projected/core/SharedProjectedRenderItems.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneTrace.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"

#include <algorithm>

#include <glm/gtc/type_ptr.hpp>

namespace persistent = game::runtime::shared_projected_render_items;
namespace world_scene_trace = game::runtime::shared_projected_unit_world_scene_trace;

namespace game::runtime::shared_projected_unit_world_scene::submission {

namespace {

std::uint64_t fnv1a64Append(std::uint64_t hash, const void* data, std::size_t byteCount) {
    static constexpr std::uint64_t kPrime = 1099511628211ull;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < byteCount; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= kPrime;
    }
    return hash;
}

} // namespace

std::array<float, 16> buildRigidBatchModelMatrix(
    const game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState& prepared,
    const game::runtime::shared_backend_pose::PoseEval* scenePose,
    int resolvedTriNodeIndex) {
    std::array<float, 16> outModelMatrix = prepared.modelMatrix;
    if (!scenePose ||
        !scenePose->hasScenePose ||
        resolvedTriNodeIndex < 0 ||
        static_cast<std::size_t>(resolvedTriNodeIndex) >= scenePose->nodeGlobals.size()) {
        return outModelMatrix;
    }

    const glm::mat4 batchModel =
        prepared.modelM * scenePose->nodeGlobals[static_cast<std::size_t>(resolvedTriNodeIndex)];
    const float* batchModelData = glm::value_ptr(batchModel);
    std::copy(batchModelData, batchModelData + 16u, outModelMatrix.begin());
    return outModelMatrix;
}

SubmissionSummary appendWorldSceneInstances(
    const game::runtime::shared_projected_unit_models::Args& args,
    const game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState& prepared,
    const game::runtime::shared_projected_unit_backend_mesh_support::FastTexturedMeshTemplateCache&
        fastCache,
    const game::runtime::shared_projected_unit_backend_mesh_support::
        FastTexturedMaterialTemplateCache& materialCache,
    const game::runtime::shared_projected_unit_world_scene::batch_state::ResolvedBatchState&
        batchState,
    bool traceThisUnit) {
    SubmissionSummary summary{};
    summary.batchHash = traceThisUnit ? 14695981039346656037ull : 0ull;
    const auto sceneColor = prepared.fastTexturedTint;
    const float sceneAlpha = prepared.fastTexturedAlpha;

    for (std::size_t fastBatchIndex = 0; fastBatchIndex < fastCache.batches.size();
         ++fastBatchIndex) {
        const auto& batchTemplate = fastCache.batches[fastBatchIndex];
        const auto& materialTemplate =
            materialCache.materials[batchTemplate.baseSubmeshIndex];
        const float visibilityAlpha =
            batchTemplate.baseSubmeshIndex <
                    prepared.submeshVisibilityAlpha.size()
                ? prepared.submeshVisibilityAlpha[
                      batchTemplate.baseSubmeshIndex]
                : 1.0f;
        const float instanceAlpha = sceneAlpha * visibilityAlpha;

        persistent::ProjectedRenderItemKey itemKey{};
        itemKey.unitId = args.unit->id;
        itemKey.mesh = prepared.mesh;
        itemKey.itemIndex = static_cast<std::uint32_t>(fastBatchIndex);
        itemKey.materialVariant = static_cast<std::uint8_t>(
            std::clamp(args.graphicsQuality, 0, 3) * 2 +
            (args.characterInkingEnabled ? 1 : 0));
        auto& itemEntry =
            persistent::ensureProjectedRenderItem(*args.projectedRenderItems, itemKey);
        persistent::touchProjectedRenderItem(*args.projectedRenderItems, itemEntry);

        if (!itemEntry.worldSceneObjectHandle ||
            itemEntry.worldSceneRegistryGeneration != args.worldSceneRegistry->generation) {
            const bool needsSceneSkinning =
                batchTemplate.skinnedBatch || !batchTemplate.gpuJointPalette.empty();
            const auto geometryHandle = shared_world_scene::ensureRigidGeometry(
                *args.worldSceneRegistry,
                &batchTemplate,
                batchTemplate.geometryCacheKey.c_str(),
                batchTemplate.gpuTemplateVertices.data(),
                batchTemplate.gpuTemplateVertices.size(),
                batchTemplate.indices.data(),
                batchTemplate.indices.size());
            const auto materialHandle = shared_world_scene::ensureMaterial(
                *args.worldSceneRegistry,
                &materialTemplate,
                materialTemplate);
            itemEntry.worldSceneObjectHandle = shared_world_scene::ensureRenderObject(
                *args.worldSceneRegistry,
                geometryHandle,
                materialHandle,
                shared_world_scene::PipelineVariant::OpaqueLit,
                static_cast<std::uint32_t>(fastBatchIndex),
                needsSceneSkinning);
            itemEntry.worldSceneRegistryGeneration = args.worldSceneRegistry->generation;
        }

        IRenderBackend::WorldSceneRenderInstanceHandle instanceHandle{};
        instanceHandle.id =
            (static_cast<std::uint32_t>(args.unit->id) << 16u) ^
            static_cast<std::uint32_t>(fastBatchIndex + 1u);
        if (batchState.batchUsesSceneSkinning[fastBatchIndex] != 0u) {
            const auto& skinState = batchState.batchSkinStates[fastBatchIndex];
            if (traceThisUnit) {
                const std::uint64_t skinHash = world_scene_trace::hashSkinPayload(skinState);
                summary.batchHash = fnv1a64Append(summary.batchHash, &skinHash, sizeof(skinHash));
                summary.batchHash = fnv1a64Append(
                    summary.batchHash,
                    &batchTemplate.baseSubmeshIndex,
                    sizeof(batchTemplate.baseSubmeshIndex));
            }
            ++summary.skinnedBatchCount;
            shared_world_scene::appendSkinnedInstance(
                *args.worldSceneFrame,
                itemEntry.worldSceneObjectHandle,
                instanceHandle,
                skinState.modelMatrix,
                sceneColor.r,
                sceneColor.g,
                sceneColor.b,
                instanceAlpha,
                prepared.indexedBatchSortDepth,
                skinState.gpuSkinningMode,
                skinState.skinMatrixCount,
                skinState.sharedSkinMatrices);
        } else {
            const std::array<float, 16> batchModelMatrix = buildRigidBatchModelMatrix(
                prepared,
                args.scenePose,
                batchState.resolvedTriNodeIndices[fastBatchIndex]);
            if (traceThisUnit) {
                summary.batchHash = fnv1a64Append(
                    summary.batchHash,
                    batchModelMatrix.data(),
                    batchModelMatrix.size() * sizeof(float));
                summary.batchHash = fnv1a64Append(
                    summary.batchHash,
                    &batchTemplate.baseSubmeshIndex,
                    sizeof(batchTemplate.baseSubmeshIndex));
            }
            ++summary.rigidBatchCount;
            shared_world_scene::appendRigidInstance(
                *args.worldSceneFrame,
                itemEntry.worldSceneObjectHandle,
                instanceHandle,
                batchModelMatrix,
                sceneColor.r,
                sceneColor.g,
                sceneColor.b,
                instanceAlpha,
                prepared.indexedBatchSortDepth);
        }
    }

    return summary;
}

} // namespace game::runtime::shared_projected_unit_world_scene::submission

