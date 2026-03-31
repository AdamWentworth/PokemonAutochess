#include <string>

#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/shared/projected/SharedPreviewBodyPresentationPath.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.h"

bool test_shared_preview_body_presentation_path_contract(std::string& outFail) {
    using game::runtime::session_render_scratch::RenderScratch;
    namespace body_path = game::runtime::shared_preview_body_presentation_path;
    namespace tail_fire = game::runtime::shared_tail_fire_playback_policy;

    RenderScratch scratch;
    if (body_path::classifyPreviewBodyPath(scratch) !=
        body_path::PreviewBodyPathDecision::DirectAnimatedFallback) {
        outFail = "Empty scratch should keep the preview on the direct animated fallback.";
        return false;
    }

    scratch.worldSceneFrame.drawClasses.emplace_back();
    if (body_path::classifyPreviewBodyPath(scratch, true) !=
        body_path::PreviewBodyPathDecision::ProjectedWorldScene) {
        outFail = "World-scene draw classes should enable the projected preview body path when supported.";
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
    if (body_path::classifyPreviewBodyPath(scratch) !=
        body_path::PreviewBodyPathDecision::DirectAnimatedFallback) {
        outFail = "Authored Tail Fire sidecar batches should keep the preview on the direct animated fallback.";
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
    if (body_path::classifyPreviewBodyPath(scratch) !=
        body_path::PreviewBodyPathDecision::DirectAnimatedFallback) {
        outFail = "Indexed-only body batches should still keep preview on the direct animated fallback until their material path is proven faithful.";
        return false;
    }

    return true;
}
