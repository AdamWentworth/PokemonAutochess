#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneRenderer.h"

#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneBatchState.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneSubmission.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneTrace.h"

#include <chrono>

namespace support = game::runtime::shared_projected_unit_backend_mesh_support;
namespace prep = game::runtime::shared_projected_unit_backend_mesh_prep;
namespace world_scene_batch_state = game::runtime::shared_projected_unit_world_scene::batch_state;
namespace world_scene_submission = game::runtime::shared_projected_unit_world_scene::submission;
namespace world_scene_trace = game::runtime::shared_projected_unit_world_scene_trace;

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

    world_scene_trace::traceEnter(args);
    const bool traceThisUnit = args.unit && world_scene_trace::shouldTraceUnit(*args.unit);

    if (world_scene_trace::shouldDisableUnit(*args.unit)) {
        world_scene_trace::traceSkip(args, "disabled_by_env");
        return false;
    }

    const bool enableGpuClipSkinning = args.enableGpuClipSkinning;

    using Clock = std::chrono::high_resolution_clock;
    const auto prepStart = Clock::now();

    prep::PreparedState prepared;
    if (!prep::prepareProjectedUnitBackendMeshWorldScene(args, out, prepared)) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        world_scene_trace::traceSkip(args, "prepare_failed");
        return out.skipUnit;
    }

    if (!prepared.useFastTexturedFullMeshPath ||
        !prepared.fullIndexedMeshPath ||
        !prepared.mesh ||
        !prepared.submeshNodeFallback ||
        prepared.modelIndexedBatchCount == 0u) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        world_scene_trace::traceSkip(args, "prepared_not_world_scene_eligible");
        return false;
    }

    const bool preferFullGpuSkinning =
        support::backendPrefersFullGpuSkinning(args.backendId);
    const support::FastTexturedMeshTemplateCache* fastCache =
        support::ensureFastTexturedMeshTemplateCache(
            prepared.mesh,
            *prepared.submeshNodeFallback,
            prepared.modelIndexedBatchCount,
            preferFullGpuSkinning);
    if (!fastCache || fastCache->batches.empty()) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        world_scene_trace::traceSkip(args, "fast_cache_empty");
        return false;
    }
    const support::FastTexturedMaterialTemplateCache* materialCache =
        support::ensureFastTexturedMaterialTemplateCache(
            prepared.mesh,
            prepared.modelIndexedBatchCount,
            args.characterInkingEnabled,
            args.graphicsQuality);
    if (!materialCache ||
        materialCache->materials.size() != prepared.modelIndexedBatchCount) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        world_scene_trace::traceSkip(args, "material_cache_mismatch");
        return false;
    }
    IRenderBackend::WorldSceneFastPathCaps fastPathCaps{};
    (void)args.renderer->getWorldSceneFastPathCaps(fastPathCaps);

    for (std::size_t fastBatchIndex = 0; fastBatchIndex < fastCache->batches.size();
         ++fastBatchIndex) {
        const auto& batchTemplate = fastCache->batches[fastBatchIndex];
        if (batchTemplate.gpuTemplateVertices.empty() ||
            batchTemplate.indices.size() < 3u ||
            batchTemplate.baseSubmeshIndex >= materialCache->materials.size()) {
            if (args.perfBreakdown) {
                args.perfBreakdown->prepMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
            }
            world_scene_trace::traceSkip(args, "batch_validation_failed");
            return false;
        }

        const auto& materialTemplate =
            materialCache->materials[batchTemplate.baseSubmeshIndex];
        if (prepared.fastTexturedAlpha < 0.999f ||
            materialTemplate.blendMode != 0u ||
            materialTemplate.materialMode != 2u ||
            materialTemplate.alphaMode == 2u) {
            if (args.perfBreakdown) {
                args.perfBreakdown->prepMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
            }
            world_scene_trace::traceSkip(args, "material_validation_failed");
            return false;
        }
    }

    world_scene_batch_state::ResolvedBatchState batchState;
    if (!world_scene_batch_state::resolveBatchState(
            args,
            prepared,
            *fastCache,
            fastPathCaps,
            enableGpuClipSkinning,
            batchState)) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        world_scene_trace::traceSkip(args, "skinned_batch_state_unavailable");
        return false;
    }

    if (args.perfBreakdown) {
        args.perfBreakdown->prepMs +=
            std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
    }

    const std::uint64_t poseHash =
        traceThisUnit ? world_scene_trace::hashPoseEval(args.scenePose) : 0ull;
    const auto submissionSummary = world_scene_submission::appendWorldSceneInstances(
        args,
        prepared,
        *fastCache,
        *materialCache,
        batchState,
        traceThisUnit);

    if (submissionSummary.rigidBatchCount == 0u &&
        submissionSummary.skinnedBatchCount == 0u) {
        world_scene_trace::traceSkip(args, "no_renderable_batches");
        return false;
    }

    world_scene_trace::traceFrameSummary(
        args,
        prepared,
        submissionSummary.rigidBatchCount,
        submissionSummary.skinnedBatchCount,
        submissionSummary.batchHash,
        poseHash);
    out.drewModelMesh = true;
    return true;
}

} // namespace game::runtime::shared_projected_unit_world_scene

