#include "game/runtime/session/SessionFrameMetrics.h"

#include <string>

bool test_session_frame_metrics_contract(std::string& outFail) {
    EngineServices services;
    services.frameRenderBuildBreakdown.overlayPrepMs = 9.0f;

    game::runtime::session_frame_metrics::publish(
        &services,
        {
            .visibleAnimatedUnits = 7u,
            .particleCount = 4u,
            .projectedUnitsMs = 3.5f,
            .projectedPoseEvalMs = 0.5f,
            .projectedModelMs = 2.0f,
            .projectedModelPrepMs = 0.7f,
            .projectedModelGeometryMs = 1.3f,
            .projectedOverlayMs = 0.3f,
            .projectedUnitsProcessed = 6u,
            .projectedModelUnits = 5u,
            .projectedClipSkinnedUnits = 4u,
            .worldComposeMs = 8.0f,
            .worldBackdropMs = 1.2f,
            .worldVfxMs = 0.6f,
            .worldDepthFlushMs = 0.4f,
        });

    if (services.frameVisibleAnimatedUnits != 7u ||
        services.frameParticleCount != 4u ||
        services.frameProjectedUnitsProcessed != 6u ||
        services.frameProjectedModelUnits != 5u ||
        services.frameProjectedClipSkinnedUnits != 4u) {
        outFail =
            "SessionFrameMetrics should publish projected counters and visible-unit totals into EngineServices.";
        return false;
    }

    if (services.frameRenderBuildBreakdown.worldComposeMs != 4.5f ||
        services.frameRenderBuildBreakdown.worldBackdropMs != 1.2f ||
        services.frameRenderBuildBreakdown.worldVfxMs != 0.6f ||
        services.frameRenderBuildBreakdown.worldDepthFlushMs != 0.4f ||
        services.frameRenderBuildBreakdown.overlayPrepMs != 0.0f) {
        outFail =
            "SessionFrameMetrics should reset the frame render breakdown and republish the world composition metrics.";
        return false;
    }

    return true;
}
