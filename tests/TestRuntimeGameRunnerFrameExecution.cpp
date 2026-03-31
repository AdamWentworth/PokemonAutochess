#include <cmath>
#include <string>

#include "engine/core/EngineServices.h"
#include "engine/core/GameLoop.h"
#include "engine/runtime/FixedStep.h"
#include "game/runtime/loop/RuntimeGameRunnerFrameExecution.h"

namespace {

class RecordingLoop final : public GameLoop {
public:
    void init(GameContext&) override {}
    void handleEvent(const InputEvent&) override {}

    void fixedUpdate(float dt) override {
        ++fixedUpdateCalls;
        lastFixedDt = dt;
        services->frameFixedBreakdown.combatMs += 1.0f;
    }

    void render(int drawableW, int drawableH) override {
        ++renderCalls;
        lastRenderW = drawableW;
        lastRenderH = drawableH;
    }

    void shutdown() override {}

    EngineServices* services = nullptr;
    int fixedUpdateCalls = 0;
    int renderCalls = 0;
    int lastRenderW = 0;
    int lastRenderH = 0;
    float lastFixedDt = 0.0f;
};

} // namespace

bool test_runtime_game_runner_frame_execution_contract(std::string& outFail) {
    EngineServices services;
    services.frameVisibleAnimatedUnits = 9u;
    services.frameParticleCount = 24u;
    services.frameProjectedUnitsMs = 1.5f;
    services.frameRenderBuildBreakdown.worldComposeMs = 0.75f;

    RecordingLoop game;
    game.services = &services;
    int swapCount = 0;

    const auto result = game::runtime::runner_frame_execution::execute({
        .accumulator = engine::runtime::fixed_step::kSeconds * 2.0,
        .frameDt = engine::runtime::fixed_step::kSeconds * 2.0,
        .maxFixedTicksPerFrame = 4,
        .drawableW = 1280,
        .drawableH = 720,
        .services = services,
        .game = game,
        .renderer = nullptr,
        .swapBuffers = [&swapCount]() { ++swapCount; },
    });

    if (game.fixedUpdateCalls != 2 ||
        game.renderCalls != 1 ||
        game.lastRenderW != 1280 ||
        game.lastRenderH != 720 ||
        std::fabs(game.lastFixedDt - engine::runtime::fixed_step::kSeconds) > 0.0001f) {
        outFail = "Frame execution should drive fixedUpdate with the fixed timestep and render once at the provided drawable size.";
        return false;
    }

    if (swapCount != 1 ||
        result.rendererHandlesPresentation ||
        result.fixedPhase.fixedTicks != 2 ||
        result.fixedPhase.fixedTicksDropped != 0 ||
        result.serviceSnapshot.visibleAnimatedUnits != 9u ||
        result.serviceSnapshot.particleCount != 24u ||
        std::fabs(result.serviceSnapshot.projectedUnitsMs - 1.5f) > 0.0001f ||
        std::fabs(result.serviceSnapshot.rawRenderBreakdown.worldComposeMs - 0.75f) > 0.0001f) {
        outFail = "Frame execution should capture a service snapshot and use the swap callback when no renderer handles presentation.";
        return false;
    }

    if (result.accumulator < 0.0 ||
        result.accumulator >= engine::runtime::fixed_step::kSeconds ||
        result.frameCpuMs < 0.0 ||
        result.renderBuildMs < 0.0 ||
        result.submitRawMs < 0.0 ||
        result.backendPerf.presentWaitMs < 0.0) {
        outFail = "Frame execution should return sane non-negative timing outputs and preserve the residual accumulator.";
        return false;
    }

    return true;
}
