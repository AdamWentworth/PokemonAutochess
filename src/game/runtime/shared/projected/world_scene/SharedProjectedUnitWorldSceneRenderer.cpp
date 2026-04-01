#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneRenderer.h"

#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneBatchState.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneSubmission.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneTailFireSidecar.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneTrace.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireCoordinator.h"

#include <chrono>
#include <vector>

namespace support = game::runtime::shared_projected_unit_backend_mesh_support;
namespace prep = game::runtime::shared_projected_unit_backend_mesh_prep;
namespace tail_fire = game::runtime::shared_tail_fire_coordinator;
namespace world_scene_batch_state = game::runtime::shared_projected_unit_world_scene::batch_state;
namespace world_scene_submission = game::runtime::shared_projected_unit_world_scene::submission;
namespace world_scene_tail_fire_sidecar =
    game::runtime::shared_projected_unit_world_scene::tail_fire_sidecar;
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

    // Tail-fire playback species use a hybrid path here: the body stays on the
    // world-scene fast path while authored fire-mesh batches are emitted through
    // a dedicated indexed sidecar so the fire contract stays explicit.
    const bool tailFireMeshPlaybackSpecies = tail_fire::unitUsesTailFireMeshPlayback(*args.unit);
    const bool enableGpuClipSkinning =
        args.enableGpuClipSkinning &&
        tail_fire::backendUsesGpuClipSkinning(args.backendId, args.unit->name);

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
    const auto* tailFireProfile =
        tail_fire::resolvePlaybackProfile(args.unit->name, prepared.mesh);
    if (tailFireMeshPlaybackSpecies &&
        (!args.sharedTailFireAnchors || !args.worldIndexedBatches || !args.ensureBackendTextureLoaded)) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        world_scene_trace::traceSkip(args, "tail_fire_sidecar_args_missing");
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
        if (world_scene_submission::batchUsesTailFireSubmesh(batchTemplate, tailFireProfile)) {
            continue;
        }
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

    std::vector<shared_world_batches::WorldIndexedBatch> tailFireSidecarBatches;
    if (tailFireMeshPlaybackSpecies) {
        batchState.ensureTransformsInitialized(args, prepared);
        auto& anchor = (*args.sharedTailFireAnchors)[args.unit->id];
        if (!tail_fire::exportPlaybackAnchor(
                {
                    .unitId = args.unit->id,
                    .mesh = prepared.mesh,
                    .scenePose = prepared.scenePose,
                    .resolvedScaleCorrection = prepared.resolvedScaleCorrection,
                    .config = &game::runtime::shared_projected_scene::getPrimaryTailFireConfig(),
                    .worldMatrixForNode =
                        [&](int nodeIndex) {
                            return batchState.transforms.worldMatrixForNode(nodeIndex);
                        },
                    .logDebug =
                        support::tailFireDebugShouldLogAnchor(args.tailFireDebugEnabled, args.unit->id),
                },
                anchor)) {
            anchor = {};
        }
        if (tailFireProfile && tailFireProfile->hasFireSubmesh &&
            !world_scene_tail_fire_sidecar::buildTailFireSidecarBatches(
                args,
                prepared,
                *fastCache,
                *tailFireProfile,
                batchState,
                tailFireSidecarBatches)) {
            world_scene_trace::traceSkip(args, "tail_fire_sidecar_unavailable");
            return false;
        }
    }

    const std::uint64_t poseHash =
        traceThisUnit ? world_scene_trace::hashPoseEval(args.scenePose) : 0ull;
    const auto submissionSummary = world_scene_submission::appendWorldSceneInstances(
        args,
        prepared,
        *fastCache,
        *materialCache,
        tailFireProfile,
        batchState,
        traceThisUnit);

    if (submissionSummary.rigidBatchCount == 0u &&
        submissionSummary.skinnedBatchCount == 0u &&
        tailFireSidecarBatches.empty()) {
        world_scene_trace::traceSkip(args, "no_renderable_batches");
        return false;
    }
    if (args.worldIndexedBatches && !tailFireSidecarBatches.empty()) {
        args.worldIndexedBatches->reserve(
            args.worldIndexedBatches->size() + tailFireSidecarBatches.size());
        for (auto& batch : tailFireSidecarBatches) {
            args.worldIndexedBatches->push_back(std::move(batch));
        }
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

