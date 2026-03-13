#include "game/runtime/loop/RuntimePerfAccumulator.h"

#include <algorithm>
#include <cmath>

namespace {

void accumulateRenderBreakdown(EngineRenderBuildBreakdown& total,
                               const EngineRenderBuildBreakdown& frame) {
    total.worldComposeMs += frame.worldComposeMs;
    total.worldBackdropMs += frame.worldBackdropMs;
    total.worldVfxMs += frame.worldVfxMs;
    total.worldDepthFlushMs += frame.worldDepthFlushMs;
    total.overlayPrepMs += frame.overlayPrepMs;
    total.worldBackgroundMs += frame.worldBackgroundMs;
    total.worldTriangles3dMs += frame.worldTriangles3dMs;
    total.worldIndexedMs += frame.worldIndexedMs;
    total.worldDebugMs += frame.worldDebugMs;
    total.spriteMs += frame.spriteMs;
    total.uiMs += frame.uiMs;
    total.otherMs += frame.otherMs;
}

EngineRenderBuildBreakdown averageRenderBreakdown(const EngineRenderBuildBreakdown& total,
                                                  double frames) {
    EngineRenderBuildBreakdown out{};
    out.worldComposeMs = static_cast<float>(total.worldComposeMs / frames);
    out.worldBackdropMs = static_cast<float>(total.worldBackdropMs / frames);
    out.worldVfxMs = static_cast<float>(total.worldVfxMs / frames);
    out.worldDepthFlushMs = static_cast<float>(total.worldDepthFlushMs / frames);
    out.overlayPrepMs = static_cast<float>(total.overlayPrepMs / frames);
    out.worldBackgroundMs = static_cast<float>(total.worldBackgroundMs / frames);
    out.worldTriangles3dMs = static_cast<float>(total.worldTriangles3dMs / frames);
    out.worldIndexedMs = static_cast<float>(total.worldIndexedMs / frames);
    out.worldDebugMs = static_cast<float>(total.worldDebugMs / frames);
    out.spriteMs = static_cast<float>(total.spriteMs / frames);
    out.uiMs = static_cast<float>(total.uiMs / frames);
    out.otherMs = static_cast<float>(total.otherMs / frames);
    return out;
}

void accumulateFixedBreakdown(EngineFixedPerfBreakdown& total,
                              const EngineFixedPerfBreakdown& frame) {
    total.preUpdateMs += frame.preUpdateMs;
    total.updatePhaseMs += frame.updatePhaseMs;
    total.postUpdateMs += frame.postUpdateMs;
    total.postOtherMs += frame.postOtherMs;
    total.phaseTransitionMs += frame.phaseTransitionMs;
    total.backendHydrateMs += frame.backendHydrateMs;
    total.cameraMs += frame.cameraMs;
    total.unitInteractionMs += frame.unitInteractionMs;
    total.shopMs += frame.shopMs;
    total.roundMs += frame.roundMs;
    total.stateManagerMs += frame.stateManagerMs;
    total.stateUpdateMs += frame.stateUpdateMs;
    total.stateFlushMs += frame.stateFlushMs;
    total.movementMs += frame.movementMs;
    total.movementPlanMs += frame.movementPlanMs;
    total.movementLuaMs += frame.movementLuaMs;
    total.movementFlushMs += frame.movementFlushMs;
    total.movementAdvanceMs += frame.movementAdvanceMs;
    total.combatMs += frame.combatMs;
    total.combatPlanMs += frame.combatPlanMs;
    total.combatLuaMs += frame.combatLuaMs;
    total.combatFlushMs += frame.combatFlushMs;
    total.worldMs += frame.worldMs;
}

EngineFixedPerfBreakdown averageFixedBreakdown(const EngineFixedPerfBreakdown& total,
                                               double frames) {
    EngineFixedPerfBreakdown out{};
    out.preUpdateMs = static_cast<float>(total.preUpdateMs / frames);
    out.updatePhaseMs = static_cast<float>(total.updatePhaseMs / frames);
    out.postUpdateMs = static_cast<float>(total.postUpdateMs / frames);
    out.postOtherMs = static_cast<float>(total.postOtherMs / frames);
    out.phaseTransitionMs = static_cast<float>(total.phaseTransitionMs / frames);
    out.backendHydrateMs = static_cast<float>(total.backendHydrateMs / frames);
    out.cameraMs = static_cast<float>(total.cameraMs / frames);
    out.unitInteractionMs = static_cast<float>(total.unitInteractionMs / frames);
    out.shopMs = static_cast<float>(total.shopMs / frames);
    out.roundMs = static_cast<float>(total.roundMs / frames);
    out.stateManagerMs = static_cast<float>(total.stateManagerMs / frames);
    out.stateUpdateMs = static_cast<float>(total.stateUpdateMs / frames);
    out.stateFlushMs = static_cast<float>(total.stateFlushMs / frames);
    out.movementMs = static_cast<float>(total.movementMs / frames);
    out.movementPlanMs = static_cast<float>(total.movementPlanMs / frames);
    out.movementLuaMs = static_cast<float>(total.movementLuaMs / frames);
    out.movementFlushMs = static_cast<float>(total.movementFlushMs / frames);
    out.movementAdvanceMs = static_cast<float>(total.movementAdvanceMs / frames);
    out.combatMs = static_cast<float>(total.combatMs / frames);
    out.combatPlanMs = static_cast<float>(total.combatPlanMs / frames);
    out.combatLuaMs = static_cast<float>(total.combatLuaMs / frames);
    out.combatFlushMs = static_cast<float>(total.combatFlushMs / frames);
    out.worldMs = static_cast<float>(total.worldMs / frames);
    return out;
}

} // namespace

namespace game::runtime::perf_accum {

void RollingAccumulator::addFrame(const FrameSample& sample) {
    ++frameCount_;
    fpsTimer_ += sample.frameDt;
    frameMs_ += sample.frameCpuMs;
    fixedMs_ += sample.fixedMs;
    fixedTickWorkMs_ += sample.fixedTickWorkMs;
    renderBuildMs_ += sample.renderBuildMs;
    renderSubmitMs_ += sample.renderSubmitMs;
    presentWaitMs_ += sample.presentWaitMs;
    legacyRenderMs_ += sample.legacyRenderMs;
    legacySwapMs_ += sample.legacySwapMs;
    if (sample.gpuFrameValid) {
        gpuFrameMs_ += sample.gpuFrameMs;
        ++gpuFrameSamples_;
    }
    drawCalls_ += static_cast<double>(sample.drawCalls);
    triangles_ += static_cast<double>(sample.triangles);
    visibleAnimatedUnits_ += static_cast<double>(sample.visibleAnimatedUnits);
    particleCount_ += static_cast<double>(sample.particleCount);
    projectedUnitsMs_ += static_cast<double>(sample.projectedUnitsMs);
    projectedPoseEvalMs_ += static_cast<double>(sample.projectedPoseEvalMs);
    projectedModelMs_ += static_cast<double>(sample.projectedModelMs);
    projectedModelPrepMs_ += static_cast<double>(sample.projectedModelPrepMs);
    projectedModelGeometryMs_ += static_cast<double>(sample.projectedModelGeometryMs);
    projectedOverlayMs_ += static_cast<double>(sample.projectedOverlayMs);
    projectedUnitsProcessed_ += static_cast<double>(sample.projectedUnitsProcessed);
    projectedModelUnits_ += static_cast<double>(sample.projectedModelUnits);
    projectedClipSkinnedUnits_ += static_cast<double>(sample.projectedClipSkinnedUnits);
    accumulateRenderBreakdown(renderBreakdown_, sample.renderBreakdown);
    accumulateFixedBreakdown(fixedBreakdown_, sample.fixedBreakdown);
    fixedTicks_ += sample.fixedTicks;
    fixedTicksDropped_ += sample.fixedTicksDropped;
}

bool RollingAccumulator::readyToEmit() const {
    return fpsTimer_ >= 1.0;
}

WindowSummary RollingAccumulator::makeSummaryAndReset() {
    WindowSummary summary;
    const double frames = std::max(1, frameCount_);
    EngineFramePerfStats& out = summary.framePerf;
    out.fps = static_cast<float>(static_cast<double>(frameCount_) / std::max(0.000001, fpsTimer_));
    out.frameMs = static_cast<float>(frameMs_ / frames);
    out.fixedMs = static_cast<float>(fixedMs_ / frames);
    out.fixedTickMs = static_cast<float>(fixedTicks_ > 0
        ? (fixedTickWorkMs_ / static_cast<double>(fixedTicks_))
        : 0.0);
    out.renderBuildMs = static_cast<float>(renderBuildMs_ / frames);
    out.renderSubmitMs = static_cast<float>(renderSubmitMs_ / frames);
    out.presentWaitMs = static_cast<float>(presentWaitMs_ / frames);
    out.gpuFrameValid = gpuFrameSamples_ > 0;
    out.gpuFrameMs = static_cast<float>(out.gpuFrameValid
        ? (gpuFrameMs_ / static_cast<double>(gpuFrameSamples_))
        : 0.0);
    out.drawCalls = static_cast<std::uint32_t>(std::lround(drawCalls_ / frames));
    out.triangles = static_cast<std::uint64_t>(std::llround(triangles_ / frames));
    out.visibleAnimatedUnits = static_cast<std::uint32_t>(std::lround(visibleAnimatedUnits_ / frames));
    out.particleCount = static_cast<std::uint32_t>(std::lround(particleCount_ / frames));
    out.projectedUnitsMs = static_cast<float>(projectedUnitsMs_ / frames);
    out.projectedPoseEvalMs = static_cast<float>(projectedPoseEvalMs_ / frames);
    out.projectedModelMs = static_cast<float>(projectedModelMs_ / frames);
    out.projectedModelPrepMs = static_cast<float>(projectedModelPrepMs_ / frames);
    out.projectedModelGeometryMs = static_cast<float>(projectedModelGeometryMs_ / frames);
    out.projectedOverlayMs = static_cast<float>(projectedOverlayMs_ / frames);
    out.projectedUnitsProcessed = static_cast<std::uint32_t>(std::lround(projectedUnitsProcessed_ / frames));
    out.projectedModelUnits = static_cast<std::uint32_t>(std::lround(projectedModelUnits_ / frames));
    out.projectedClipSkinnedUnits =
        static_cast<std::uint32_t>(std::lround(projectedClipSkinnedUnits_ / frames));
    out.renderBreakdown = averageRenderBreakdown(renderBreakdown_, frames);
    out.renderMs = static_cast<float>(legacyRenderMs_ / frames);
    out.swapMs = static_cast<float>(legacySwapMs_ / frames);
    out.fixedTicks = static_cast<int>(std::lround(static_cast<double>(fixedTicks_) / frames));
    out.fixedTicksDropped = static_cast<int>(std::lround(static_cast<double>(fixedTicksDropped_) / frames));
    out.fixedBreakdown = averageFixedBreakdown(fixedBreakdown_, frames);

    *this = {};
    return summary;
}

} // namespace game::runtime::perf_accum

