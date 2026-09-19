#include "game/runtime/session/SessionFrameMetrics.h"

#include <string>

bool test_session_frame_metrics_contract(std::string& outFail) {
    EngineServices genericHost;
    genericHost.graphicsQuality = 2;
    GameRuntimeServices fallback;
    if (bindGameRuntimeServices(nullptr, fallback) != nullptr ||
        bindGameRuntimeServices(&genericHost, fallback) != &fallback ||
        fallback.graphicsQuality != 2) {
        outFail = "A generic host must bind isolated game-owned state and preserve engine settings.";
        return false;
    }
    GameRuntimeServices otherFallback;
    fallback.frameFixedBreakdown.shopMs = 12.0f;
    bindGameRuntimeServices(&genericHost, otherFallback);
    if (otherFallback.frameFixedBreakdown.shopMs != 0.0f) {
        outFail = "Game diagnostics must not leak between sessions sharing an engine host.";
        return false;
    }
    GameRuntimeServices services;
    services.terminalLogMode = GameTerminalLogMode::ScratchVfx;
    if (bindGameRuntimeServices(&services, fallback) != &services ||
        services.terminalLogMode != GameTerminalLogMode::ScratchVfx) {
        outFail = "Standalone and editor hosts must share their live game diagnostics without copying.";
        return false;
    }
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
            "SessionFrameMetrics should publish projected counters and visible-unit totals into GameRuntimeServices.";
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
