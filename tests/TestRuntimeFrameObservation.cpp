#include <cmath>
#include <string>

#include "engine/core/EngineServices.h"
#include "game/runtime/loop/RuntimeFrameObservation.h"

bool test_runtime_frame_observation_contract(std::string& outFail) {
    EngineServices services;
    services.frameVisibleAnimatedUnits = 12u;
    services.frameParticleCount = 34u;
    services.frameProjectedUnitsMs = 4.5f;
    services.frameProjectedPoseEvalMs = 1.25f;
    services.frameProjectedModelMs = 2.0f;
    services.frameProjectedModelPrepMs = 0.5f;
    services.frameProjectedModelGeometryMs = 0.75f;
    services.frameProjectedOverlayMs = 0.25f;
    services.frameProjectedUnitsProcessed = 18u;
    services.frameProjectedModelUnits = 7u;
    services.frameProjectedClipSkinnedUnits = 3u;
    services.frameRenderBuildBreakdown.worldComposeMs = 1.0f;
    services.frameRenderBuildBreakdown.overlayPrepMs = 0.5f;
    services.frameRenderBuildBreakdown.worldBackgroundMs = 0.25f;
    services.frameRenderBuildBreakdown.uiMs = 0.75f;

    const auto snapshot = game::runtime::frame_observation::captureServiceSnapshot(services);
    if (snapshot.visibleAnimatedUnits != 12u ||
        snapshot.particleCount != 34u ||
        std::fabs(snapshot.projectedUnitsMs - 4.5f) > 0.0001f ||
        snapshot.projectedUnitsProcessed != 18u ||
        std::fabs(snapshot.rawRenderBreakdown.worldComposeMs - 1.0f) > 0.0001f) {
        outFail = "captureServiceSnapshot should preserve the frame-level engine service counters.";
        return false;
    }

    game::runtime::frame_observation::SampleInputs inputs;
    inputs.frameDt = 0.016;
    inputs.frameCpuMs = 17.0;
    inputs.fixedMs = 5.0;
    inputs.fixedTickWorkMs = 4.0;
    inputs.renderBuildMs = 10.0;
    inputs.renderSubmitMs = 2.0;
    inputs.presentWaitMs = 1.0;
    inputs.legacyRenderMs = 9.0;
    inputs.legacySwapMs = 1.5;
    inputs.gpuFrameMs = 8.0;
    inputs.gpuFrameValid = true;
    inputs.drawCalls = 123u;
    inputs.triangles = 4567u;
    inputs.fixedBreakdown.combatMs = 2.5f;
    inputs.fixedTicks = 3;
    inputs.fixedTicksDropped = 1;

    const auto sample = game::runtime::frame_observation::makePerfSample(inputs, snapshot);
    if (std::fabs(sample.frameCpuMs - 17.0) > 0.000001 ||
        sample.visibleAnimatedUnits != 12u ||
        sample.particleCount != 34u ||
        sample.drawCalls != 123u ||
        sample.triangles != 4567u ||
        sample.fixedTicks != 3 ||
        sample.fixedTicksDropped != 1 ||
        std::fabs(sample.fixedBreakdown.combatMs - 2.5f) > 0.0001f ||
        std::fabs(sample.renderBreakdown.otherMs - 3.0f) > 0.0001f) {
        outFail = "makePerfSample should combine timing inputs with service snapshots and finalize the render breakdown.";
        return false;
    }

    return true;
}

