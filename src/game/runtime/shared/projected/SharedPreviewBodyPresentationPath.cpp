#include "game/runtime/shared/projected/SharedPreviewBodyPresentationPath.h"

#include "game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

namespace game::runtime::shared_preview_body_presentation_path {

PreviewBodyPathDecision classifyPreviewBodyPath(
    const session_render_scratch::RenderScratch& scratch,
    bool supportsWorldSceneFastPath) {
    if (supportsWorldSceneFastPath && !scratch.worldSceneFrame.drawClasses.empty()) {
        return PreviewBodyPathDecision::ProjectedWorldScene;
    }

    for (const auto& batch : scratch.worldIndexedBatches) {
        if (!batch.hasGeometry()) continue;
        if (shared_tail_fire_playback_policy::batchUsesAuthoredFireMesh(batch)) continue;

        const auto& materialBatch = shared_world_batches::resolvedMaterialBatch(batch);
        const bool hasBaseTexture = shared_world_batches::resolvedHasBaseTexture(batch);
        const bool litMaterial = materialBatch.materialMode >= 2u;
        if (hasBaseTexture && litMaterial) {
            // Preview uses the projected body path only when the backend can
            // reproduce the same world-scene material path the game relies on.
            // Indexed-only body output can still be useful scratch for Tail Fire,
            // but it is not yet a safe replacement for the direct animated draw.
            return PreviewBodyPathDecision::DirectAnimatedFallback;
        }
    }

    return PreviewBodyPathDecision::DirectAnimatedFallback;
}

} // namespace game::runtime::shared_preview_body_presentation_path
