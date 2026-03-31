#include "game/runtime/shared/projected/SharedPreviewBodyPresentationPath.h"

#include "engine/core/Environment.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

namespace game::runtime::shared_preview_body_presentation_path {

namespace {

bool envFlagEnabled(const char* name) {
    const auto env = engine::env::get(name);
    if (!env.has_value()) return false;
    const std::string raw = *env;
    if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
        return false;
    }
    return true;
}

} // namespace

bool indexedScratchPathAllowedForPreview() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_PREVIEW_ALLOW_INDEXED_BODY_PATH");
        if (!env.has_value()) return true;
        return envFlagEnabled("PAC_PREVIEW_ALLOW_INDEXED_BODY_PATH");
    }();
    return enabled;
}

PreviewBodyPathSummary inspectPreviewBodyPath(
    const session_render_scratch::RenderScratch& scratch,
    bool supportsWorldSceneFastPath) {
    PreviewBodyPathSummary summary{};
    summary.supportsWorldSceneFastPath = supportsWorldSceneFastPath;
    summary.allowIndexedScratchPath = indexedScratchPathAllowedForPreview();
    summary.worldSceneDrawClassCount = scratch.worldSceneFrame.drawClasses.size();
    summary.worldIndexedBatchCount = scratch.worldIndexedBatches.size();

    if (supportsWorldSceneFastPath && !scratch.worldSceneFrame.drawClasses.empty()) {
        summary.decision = PreviewBodyPathDecision::ProjectedWorldScene;
        return summary;
    }

    for (const auto& batch : scratch.worldIndexedBatches) {
        if (!batch.hasGeometry()) continue;
        if (shared_tail_fire_playback_policy::batchUsesAuthoredFireMesh(batch)) {
            ++summary.authoredFireBatchCount;
            continue;
        }

        const auto& materialBatch = shared_world_batches::resolvedMaterialBatch(batch);
        const bool hasBaseTexture = shared_world_batches::resolvedHasBaseTexture(batch);
        const bool litMaterial = materialBatch.materialMode >= 2u;
        if (hasBaseTexture && litMaterial) {
            ++summary.litTexturedIndexedBodyBatchCount;
        }
    }

    if (summary.litTexturedIndexedBodyBatchCount > 0u &&
        summary.allowIndexedScratchPath) {
        summary.decision = PreviewBodyPathDecision::ProjectedIndexedScratch;
        return summary;
    }

    // Preview prefers the shared projected body path whenever the backend
    // can already produce lit textured body batches. An env switch still
    // exists so we can force the direct fallback during investigation.
    summary.decision = PreviewBodyPathDecision::DirectAnimatedFallback;
    return summary;
}

PreviewBodyPathDecision classifyPreviewBodyPath(
    const session_render_scratch::RenderScratch& scratch,
    bool supportsWorldSceneFastPath) {
    return inspectPreviewBodyPath(scratch, supportsWorldSceneFastPath).decision;
}

} // namespace game::runtime::shared_preview_body_presentation_path
