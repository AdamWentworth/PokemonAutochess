#include <string>

#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/shared/projected/core/SharedPreviewBodyPresentationPath.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.h"

bool test_shared_preview_body_presentation_path_contract(std::string& outFail) {
    using game::runtime::session_render_scratch::RenderScratch;
    namespace body_path = game::runtime::shared_preview_body_presentation_path;
    namespace tail_fire = game::runtime::shared_tail_fire_playback_policy;

    RenderScratch scratch;
    const auto emptySummary = body_path::inspectPreviewBodyPath(scratch);
    if (emptySummary.decision !=
        body_path::PreviewBodyPathDecision::DirectAnimatedFallback) {
        outFail = "Empty scratch should keep the preview on the direct animated fallback.";
        return false;
    }
    if (emptySummary.worldSceneDrawClassCount != 0u ||
        emptySummary.worldIndexedBatchCount != 0u) {
        outFail = "Empty scratch summary should report zero projected body content.";
        return false;
    }

    scratch.worldSceneFrame.drawClasses.emplace_back();
    const auto worldSceneSummary = body_path::inspectPreviewBodyPath(scratch, true);
    if (worldSceneSummary.decision !=
        body_path::PreviewBodyPathDecision::ProjectedWorldScene) {
        outFail = "World-scene draw classes should enable the projected preview body path when supported.";
        return false;
    }
    if (worldSceneSummary.worldSceneDrawClassCount != 1u) {
        outFail = "World-scene summary should report the draw-class count.";
        return false;
    }
    if (body_path::classifyPreviewBodyPath(scratch, false) !=
        body_path::PreviewBodyPathDecision::DirectAnimatedFallback) {
        outFail = "World-scene draw classes should not enable the projected preview body path when the backend lacks fast-path support.";
        return false;
    }

    scratch = RenderScratch{};
    game::runtime::shared_world_batches::WorldIndexedBatch authoredFireOnly;
    authoredFireOnly.vertices.resize(3u);
    authoredFireOnly.indices = {0u, 1u, 2u};
    authoredFireOnly.materialFlags = static_cast<float>(tail_fire::kAuthoredFireMeshFlagBit);
    authoredFireOnly.materialMode = 2u;
    authoredFireOnly.textureWidth = 1;
    authoredFireOnly.textureHeight = 1;
    authoredFireOnly.ownedTextureRgba = {255u, 255u, 255u, 255u};
    scratch.worldIndexedBatches.push_back(authoredFireOnly);
    const auto authoredFireSummary = body_path::inspectPreviewBodyPath(scratch);
    if (authoredFireSummary.decision !=
        body_path::PreviewBodyPathDecision::DirectAnimatedFallback) {
        outFail = "Authored Tail Fire sidecar batches should keep the preview on the direct animated fallback.";
        return false;
    }
    if (authoredFireSummary.authoredFireBatchCount != 1u ||
        authoredFireSummary.litTexturedIndexedBodyBatchCount != 0u) {
        outFail = "Authored Tail Fire sidecar batches should be counted separately from body output.";
        return false;
    }

    scratch = RenderScratch{};
    game::runtime::shared_world_batches::WorldIndexedBatch texturedBody;
    texturedBody.vertices.resize(3u);
    texturedBody.indices = {0u, 1u, 2u};
    texturedBody.materialMode = 2u;
    texturedBody.textureWidth = 1;
    texturedBody.textureHeight = 1;
    texturedBody.ownedTextureRgba = {255u, 255u, 255u, 255u};
    scratch.worldIndexedBatches.push_back(texturedBody);
    const auto indexedBodySummary = body_path::inspectPreviewBodyPath(scratch);
    if (indexedBodySummary.decision !=
        body_path::PreviewBodyPathDecision::ProjectedIndexedScratch) {
        outFail = "Lit textured indexed body batches should use the projected indexed scratch preview path by default.";
        return false;
    }
    if (indexedBodySummary.litTexturedIndexedBodyBatchCount != 1u) {
        outFail = "Indexed body summary should count lit textured body batches.";
        return false;
    }
    if (!indexedBodySummary.allowIndexedScratchPath) {
        outFail = "Indexed scratch preview body path should default to enabled.";
        return false;
    }

    return true;
}

