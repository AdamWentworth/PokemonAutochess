#include "game/preview/PreviewAnimatedModelPresentation.h"

#include "engine/render/Camera3D.h"
#include "game/preview/PreviewPokemonVisual.h"
#include "game/preview/PreviewSceneUtils.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireCoordinator.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"

namespace game::preview::preview_animated_model_presentation {

namespace {

bool poseHasRenderableTransforms(const DirectBodySample& sample) {
    return !sample.pose.locals.empty() || !sample.pose.globals.empty();
}

} // namespace

void ensureTailFireSuppressionMask(PreviewPokemonVisual& visual) {
    if (visual.directDrawSkipSubmeshMaskReady) return;
    visual.directDrawSkipSubmeshMaskReady = true;
    visual.directDrawSkipSubmeshMask.clear();

    if (!visual.valid ||
        !visual.model ||
        !game::runtime::shared_tail_fire_coordinator::speciesUsesTailFireMeshPlayback(
            visual.speciesName)) {
        return;
    }

    visual.directDrawSkipSubmeshMask =
        visual.model->buildSubmeshMaskForNodeNameContainsInsensitive(
            std::string(game::runtime::shared_tail_fire_mesh_playback::authoredFireMeshNodeToken()));
}

bool buildDirectBodySample(const PreviewPokemonVisual& visual,
                           const glm::vec3& worldPos,
                           float yawDeg,
                           DirectBodySample& outSample,
                           float boardSurfaceY) {
    outSample = {};
    if (!visual.valid || !visual.model) return false;

    outSample.animIndex = visual.currentAnimIndex();
    outSample.animTimeSec = visual.currentAnimTimeSec();
    if (outSample.animIndex < 0) return false;

    outSample.renderPos = makeProjectedAlignedPreviewPos(visual, worldPos, boardSurfaceY);
    outSample.instanceTransform =
        makePreviewInstanceTransform(outSample.renderPos, yawDeg, visual.finalScale);
    visual.model->sampleAnimatedPose(
        outSample.animTimeSec,
        outSample.animIndex,
        outSample.pose);
    return true;
}

void drawDirectBody(const Camera3D& camera,
                    PreviewPokemonVisual& visual,
                    const DirectBodySample& sample) {
    if (!visual.valid || !visual.model || sample.animIndex < 0) return;
    ensureTailFireSuppressionMask(visual);
    visual.model->drawAnimatedSampled(
        camera,
        sample.instanceTransform,
        sample.pose,
        glm::vec3(1.0f),
        0.0f,
        visual.directDrawSkipSubmeshMask.empty() ? nullptr : &visual.directDrawSkipSubmeshMask);
}

bool buildScenePose(const DirectBodySample& sample,
                    const game::runtime::render_model::MeshData& mesh,
                    game::runtime::shared_backend_pose::PoseEval& outPose) {
    outPose = {};
    const std::size_t nodeCount = mesh.nodesDefault.size();
    if (nodeCount == 0u || !poseHasRenderableTransforms(sample)) {
        return false;
    }

    outPose.nodeLocals = sample.pose.locals;
    if (outPose.nodeLocals.size() < nodeCount) {
        outPose.nodeLocals.resize(nodeCount);
        for (std::size_t nodeIndex = sample.pose.locals.size(); nodeIndex < nodeCount; ++nodeIndex) {
            outPose.nodeLocals[nodeIndex] = mesh.nodesDefault[nodeIndex];
        }
    } else if (outPose.nodeLocals.size() > nodeCount) {
        outPose.nodeLocals.resize(nodeCount);
    }

    outPose.nodeGlobals.resize(nodeCount, glm::mat4(1.0f));
    for (std::size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
        if (nodeIndex < sample.pose.globals.size()) {
            outPose.nodeGlobals[nodeIndex] = sample.pose.globals[nodeIndex];
        } else if (nodeIndex < mesh.bindNodeGlobals.size()) {
            outPose.nodeGlobals[nodeIndex] = mesh.bindNodeGlobals[nodeIndex];
        }
    }

    outPose.hasScenePose = !outPose.nodeGlobals.empty();
    outPose.hasClipPose = sample.animIndex >= 0 && outPose.hasScenePose;
    return outPose.hasScenePose;
}

bool buildScenePose(const PreviewPokemonVisual& visual,
                    const game::runtime::render_model::MeshData& mesh,
                    game::runtime::shared_backend_pose::PoseEval& outPose) {
    DirectBodySample sample;
    if (!buildDirectBodySample(visual, glm::vec3(0.0f), 0.0f, sample)) {
        outPose = {};
        return false;
    }
    return buildScenePose(sample, mesh, outPose);
}

} // namespace game::preview::preview_animated_model_presentation
