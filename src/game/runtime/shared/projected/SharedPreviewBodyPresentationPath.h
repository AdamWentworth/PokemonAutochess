#pragma once

#include "game/runtime/session/SessionRenderScratch.h"

namespace game::runtime::shared_preview_body_presentation_path {

enum class PreviewBodyPathDecision {
    DirectAnimatedFallback,
    ProjectedWorldScene,
    ProjectedIndexedScratch,
};

struct PreviewBodyPathSummary {
    PreviewBodyPathDecision decision = PreviewBodyPathDecision::DirectAnimatedFallback;
    bool supportsWorldSceneFastPath = false;
    bool allowIndexedScratchPath = false;
    std::size_t worldSceneDrawClassCount = 0u;
    std::size_t worldIndexedBatchCount = 0u;
    std::size_t authoredFireBatchCount = 0u;
    std::size_t litTexturedIndexedBodyBatchCount = 0u;
};

bool indexedScratchPathAllowedForPreview();

PreviewBodyPathSummary inspectPreviewBodyPath(
    const session_render_scratch::RenderScratch& scratch,
    bool supportsWorldSceneFastPath = true);

PreviewBodyPathDecision classifyPreviewBodyPath(
    const session_render_scratch::RenderScratch& scratch,
    bool supportsWorldSceneFastPath = true);

} // namespace game::runtime::shared_preview_body_presentation_path
