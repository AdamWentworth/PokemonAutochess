#pragma once

#include "game/runtime/session/SessionRenderScratch.h"

namespace game::runtime::shared_preview_body_presentation_path {

enum class PreviewBodyPathDecision {
    DirectAnimatedFallback,
    ProjectedWorldScene,
};

PreviewBodyPathDecision classifyPreviewBodyPath(
    const session_render_scratch::RenderScratch& scratch,
    bool supportsWorldSceneFastPath = true);

} // namespace game::runtime::shared_preview_body_presentation_path
