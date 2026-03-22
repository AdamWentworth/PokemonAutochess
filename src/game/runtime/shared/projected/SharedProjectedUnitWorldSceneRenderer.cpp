#include "game/runtime/shared/projected/SharedProjectedUnitWorldSceneRenderer.h"

#include "game/runtime/shared/projected/SharedProjectedRenderItems.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"

#include <chrono>

namespace support = game::runtime::shared_projected_unit_backend_mesh_support;
namespace prep = game::runtime::shared_projected_unit_backend_mesh_prep;
namespace persistent = game::runtime::shared_projected_render_items;

namespace game::runtime::shared_projected_unit_world_scene {

bool tryRenderProjectedUnitModelWorldScene(
    const shared_projected_unit_models::Args& args,
    shared_projected_unit_models::Result& out) {
    if (!args.renderer ||
        !args.renderer->supportsWorldSceneFastPath() ||
        !args.worldSceneRegistry ||
        !args.worldSceneFrame ||
        !args.projectedRenderItems ||
        !args.unit ||
        !args.meshForUnit ||
        !args.scenePose ||
        !args.tint ||
        !args.modelDepthTris ||
        !args.modelDepthWorldTris ||
        !args.remainingModelTrianglesBudget ||
        !args.world3DTriangles ||
        !args.backendModelTriangleLimit ||
        !args.backendModelFullMeshEnabled ||
        !args.backendModelFastTexturedPathEnabled ||
        !args.backendModelBackfaceCullingEnabled) {
        return false;
    }

    using Clock = std::chrono::high_resolution_clock;
    const auto prepStart = Clock::now();

    prep::PreparedState prepared;
    if (!prep::prepareProjectedUnitBackendMesh(args, out, prepared)) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        return out.skipUnit;
    }

    if (!prepared.useFastTexturedFullMeshPath ||
        !prepared.fullIndexedMeshPath ||
        !prepared.mesh ||
        !prepared.submeshNodeFallback ||
        prepared.modelIndexedBatchesPerSubmesh.empty()) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        return false;
    }

    const bool preferFullGpuSkinning =
        support::backendPrefersFullGpuSkinning(args.backendId);
    const support::FastTexturedMeshTemplateCache* fastCache =
        support::ensureFastTexturedMeshTemplateCache(
            prepared.mesh,
            *prepared.submeshNodeFallback,
            prepared.modelIndexedBatchesPerSubmesh.size(),
            preferFullGpuSkinning);
    if (!fastCache || fastCache->batches.empty()) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        return false;
    }

    for (std::size_t fastBatchIndex = 0; fastBatchIndex < fastCache->batches.size(); ++fastBatchIndex) {
        const auto& batchTemplate = fastCache->batches[fastBatchIndex];
        if (batchTemplate.skinnedBatch ||
            batchTemplate.gpuTemplateVertices.empty() ||
            batchTemplate.indices.size() < 3u ||
            batchTemplate.baseSubmeshIndex >= prepared.modelIndexedBatchesPerSubmesh.size()) {
            if (args.perfBreakdown) {
                args.perfBreakdown->prepMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
            }
            return false;
        }

        const auto& runtimeBatch =
            prepared.modelIndexedBatchesPerSubmesh[batchTemplate.baseSubmeshIndex];
        const auto* materialTemplate = runtimeBatch.sharedTemplate;
        if (!materialTemplate ||
            runtimeBatch.materialAlphaOverride ||
            materialTemplate->blendMode != 0u ||
            materialTemplate->materialMode != 2u ||
            materialTemplate->alphaMode == 2u) {
            if (args.perfBreakdown) {
                args.perfBreakdown->prepMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
            }
            return false;
        }
    }

    if (args.perfBreakdown) {
        args.perfBreakdown->prepMs +=
            std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
    }

    const std::uint32_t frameStamp =
        args.projectedRenderItems ? args.projectedRenderItems->currentFrameId : 0u;
    const auto sceneColor = prepared.fastTexturedTint;
    const float sceneAlpha = prepared.fastTexturedAlpha;

    for (std::size_t fastBatchIndex = 0; fastBatchIndex < fastCache->batches.size(); ++fastBatchIndex) {
        const auto& batchTemplate = fastCache->batches[fastBatchIndex];
        const auto& runtimeBatch =
            prepared.modelIndexedBatchesPerSubmesh[batchTemplate.baseSubmeshIndex];
        const auto* materialTemplate = runtimeBatch.sharedTemplate;

        persistent::ProjectedRenderItemKey itemKey{};
        itemKey.unitId = args.unit->id;
        itemKey.mesh = prepared.mesh;
        itemKey.itemIndex = static_cast<std::uint32_t>(fastBatchIndex);
        auto& itemEntry =
            persistent::ensureProjectedRenderItem(*args.projectedRenderItems, itemKey);
        persistent::touchProjectedRenderItem(*args.projectedRenderItems, itemEntry);
        persistent::syncProjectedRenderItemDynamicState(itemEntry, runtimeBatch, frameStamp);

        if (!itemEntry.worldSceneObjectHandle ||
            itemEntry.worldSceneRegistryGeneration != args.worldSceneRegistry->generation) {
            const auto geometryHandle = shared_world_scene::ensureRigidGeometry(
                *args.worldSceneRegistry,
                &batchTemplate,
                batchTemplate.geometryCacheKey.c_str(),
                batchTemplate.gpuTemplateVertices.data(),
                batchTemplate.gpuTemplateVertices.size(),
                batchTemplate.indices.data(),
                batchTemplate.indices.size());
            const auto materialHandle = shared_world_scene::ensureMaterialFromBatchTemplate(
                *args.worldSceneRegistry,
                materialTemplate,
                *materialTemplate);
            itemEntry.worldSceneObjectHandle = shared_world_scene::ensureRenderObject(
                *args.worldSceneRegistry,
                geometryHandle,
                materialHandle,
                shared_world_scene::PipelineVariant::OpaqueLit,
                static_cast<std::uint32_t>(fastBatchIndex));
            itemEntry.worldSceneRegistryGeneration = args.worldSceneRegistry->generation;
        }

        IRenderBackend::WorldSceneRenderInstanceHandle instanceHandle{};
        instanceHandle.id =
            (static_cast<std::uint32_t>(args.unit->id) << 16u) ^
            static_cast<std::uint32_t>(fastBatchIndex + 1u);
        shared_world_scene::appendRigidInstance(
            *args.worldSceneFrame,
            itemEntry.worldSceneObjectHandle,
            instanceHandle,
            runtimeBatch.modelMatrix,
            sceneColor.r,
            sceneColor.g,
            sceneColor.b,
            sceneAlpha,
            runtimeBatch.sortDepth);
    }

    out.drewModelMesh = true;
    return true;
}

} // namespace game::runtime::shared_projected_unit_world_scene
