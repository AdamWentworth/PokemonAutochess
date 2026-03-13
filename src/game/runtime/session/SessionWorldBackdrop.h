#pragma once

#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/shared/projected/SharedProjectedDebugVfx.h"

namespace game::runtime::session_world_backdrop {

struct ProjectedBackdropArgs {
    bool supportsWorldTriangles3D = false;
    int rows = 0;
    int cols = 0;
    int benchSlots = 0;
    float worldCellSize = 1.0f;
    float boardMinX = 0.0f;
    float boardMinZ = 0.0f;
    float boardMaxX = 0.0f;
    float boardMaxZ = 0.0f;
    float boardX = 0.0f;
    float boardY = 0.0f;
    float boardW = 0.0f;
    float boardH = 0.0f;
    float cellW = 0.0f;
    float cellH = 0.0f;
    float line = 1.0f;
};

float composeProjectedBackdrop(
    const ProjectedBackdropArgs& args,
    shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
    session_render_scratch::RenderScratch& scratch);

} // namespace game::runtime::session_world_backdrop
