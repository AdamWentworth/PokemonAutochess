#include "game/runtime/session/SessionFrameMetrics.h"

#include <algorithm>

namespace game::runtime::session_frame_metrics {

void publish(EngineServices* engineServices, const FrameMetrics& metrics) {
    if (!engineServices) return;

    engineServices->frameVisibleAnimatedUnits = metrics.visibleAnimatedUnits;
    engineServices->frameParticleCount = metrics.particleCount;
    engineServices->frameProjectedUnitsMs = metrics.projectedUnitsMs;
    engineServices->frameProjectedPoseEvalMs = metrics.projectedPoseEvalMs;
    engineServices->frameProjectedModelMs = metrics.projectedModelMs;
    engineServices->frameProjectedModelPrepMs = metrics.projectedModelPrepMs;
    engineServices->frameProjectedModelGeometryMs = metrics.projectedModelGeometryMs;
    engineServices->frameProjectedOverlayMs = metrics.projectedOverlayMs;
    engineServices->frameProjectedUnitsProcessed = metrics.projectedUnitsProcessed;
    engineServices->frameProjectedModelUnits = metrics.projectedModelUnits;
    engineServices->frameProjectedClipSkinnedUnits = metrics.projectedClipSkinnedUnits;
    engineServices->frameRenderBuildBreakdown = {};
    engineServices->frameRenderBuildBreakdown.worldComposeMs =
        std::max(0.0f, metrics.worldComposeMs - metrics.projectedUnitsMs);
    engineServices->frameRenderBuildBreakdown.worldBackdropMs =
        metrics.worldBackdropMs;
    engineServices->frameRenderBuildBreakdown.worldVfxMs = metrics.worldVfxMs;
    engineServices->frameRenderBuildBreakdown.worldDepthFlushMs =
        metrics.worldDepthFlushMs;
}

} // namespace game::runtime::session_frame_metrics
