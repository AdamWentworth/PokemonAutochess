#include <cmath>
#include <string>

#include "game/runtime/loop/RuntimePerfAccumulator.h"

bool test_runtime_perf_accumulator_contract(std::string& outFail) {
    using game::runtime::perf_accum::FrameSample;
    using game::runtime::perf_accum::RollingAccumulator;

    RollingAccumulator accum;

    FrameSample a;
    a.frameDt = 0.6;
    a.frameCpuMs = 16.0;
    a.fixedMs = 4.0;
    a.fixedTickWorkMs = 3.0;
    a.renderBuildMs = 5.0;
    a.renderSubmitMs = 2.0;
    a.presentWaitMs = 1.0;
    a.legacyRenderMs = 6.0;
    a.legacySwapMs = 1.5;
    a.gpuFrameMs = 7.0;
    a.gpuFrameValid = true;
    a.drawCalls = 100;
    a.triangles = 2000;
    a.visibleAnimatedUnits = 8;
    a.particleCount = 30;
    a.projectedUnitsMs = 2.0f;
    a.projectedPoseEvalMs = 0.5f;
    a.projectedModelMs = 1.0f;
    a.projectedModelPrepMs = 0.25f;
    a.projectedModelGeometryMs = 0.25f;
    a.projectedOverlayMs = 0.5f;
    a.projectedUnitsProcessed = 12;
    a.projectedModelUnits = 5;
    a.projectedClipSkinnedUnits = 2;
    a.renderBreakdown.worldComposeMs = 1.0f;
    a.fixedBreakdown.combatMs = 1.5f;
    a.fixedTicks = 2;
    a.fixedTicksDropped = 1;
    accum.addFrame(a);
    if (accum.readyToEmit()) {
        outFail = "RollingAccumulator should not emit until at least one second of samples is accumulated.";
        return false;
    }

    FrameSample b = a;
    b.frameDt = 0.5;
    b.frameCpuMs = 20.0;
    b.fixedMs = 6.0;
    b.fixedTickWorkMs = 5.0;
    b.renderBuildMs = 7.0;
    b.renderSubmitMs = 4.0;
    b.presentWaitMs = 3.0;
    b.legacyRenderMs = 8.0;
    b.legacySwapMs = 3.5;
    b.gpuFrameMs = 0.0;
    b.gpuFrameValid = false;
    b.drawCalls = 300;
    b.triangles = 4000;
    b.visibleAnimatedUnits = 12;
    b.particleCount = 50;
    b.projectedUnitsMs = 4.0f;
    b.projectedPoseEvalMs = 1.5f;
    b.projectedModelMs = 2.0f;
    b.projectedModelPrepMs = 0.5f;
    b.projectedModelGeometryMs = 0.75f;
    b.projectedOverlayMs = 1.0f;
    b.projectedUnitsProcessed = 18;
    b.projectedModelUnits = 7;
    b.projectedClipSkinnedUnits = 3;
    b.renderBreakdown.worldComposeMs = 3.0f;
    b.fixedBreakdown.combatMs = 2.5f;
    b.fixedTicks = 4;
    b.fixedTicksDropped = 3;
    accum.addFrame(b);
    if (!accum.readyToEmit()) {
        outFail = "RollingAccumulator should emit after at least one second of samples.";
        return false;
    }

    const auto summary = accum.makeSummaryAndReset();
    const auto& perf = summary.framePerf;
    if (std::fabs(perf.fps - (2.0f / 1.1f)) > 0.01f ||
        std::fabs(perf.frameMs - 18.0f) > 0.001f ||
        std::fabs(perf.fixedMs - 5.0f) > 0.001f ||
        std::fabs(perf.fixedTickMs - (8.0f / 6.0f)) > 0.001f ||
        std::fabs(perf.renderBuildMs - 6.0f) > 0.001f ||
        std::fabs(perf.renderSubmitMs - 3.0f) > 0.001f ||
        std::fabs(perf.presentWaitMs - 2.0f) > 0.001f ||
        !perf.gpuFrameValid ||
        std::fabs(perf.gpuFrameMs - 7.0f) > 0.001f ||
        perf.drawCalls != 200 ||
        perf.triangles != 3000 ||
        perf.visibleAnimatedUnits != 10 ||
        perf.particleCount != 40 ||
        perf.projectedUnitsProcessed != 15 ||
        perf.projectedModelUnits != 6 ||
        perf.projectedClipSkinnedUnits != 3 ||
        std::fabs(perf.renderBreakdown.worldComposeMs - 2.0f) > 0.001f ||
        std::fabs(perf.fixedBreakdown.combatMs - 2.0f) > 0.001f ||
        perf.fixedTicks != 3 ||
        perf.fixedTicksDropped != 2) {
        outFail = "RollingAccumulator should average perf fields over the sample window.";
        return false;
    }

    if (accum.readyToEmit()) {
        outFail = "RollingAccumulator should reset after emitting a summary.";
        return false;
    }

    return true;
}

