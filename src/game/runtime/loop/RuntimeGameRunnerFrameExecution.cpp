#include "game/runtime/loop/RuntimeGameRunnerFrameExecution.h"

#include "engine/core/EngineServices.h"
#include "engine/core/GameLoop.h"
#include "engine/render/IRenderBackend.h"
#include "engine/runtime/FixedStep.h"

#include <chrono>

namespace game::runtime::runner_frame_execution {

Outputs execute(const Inputs& inputs) {
    using clock = std::chrono::high_resolution_clock;

    Outputs out;
    const auto frameCpuStart = clock::now();
    out.fixedPhase = game::runtime::fixed_step_phase::execute(
        inputs.accumulator,
        engine::runtime::fixed_step::kSeconds,
        inputs.maxFixedTicksPerFrame,
        inputs.services,
        [&inputs](float dt) { inputs.game.fixedUpdate(dt); });
    out.accumulator = out.fixedPhase.accumulator;

    const auto beginFrameStart = clock::now();
    if (inputs.renderer) {
        inputs.renderer->beginFrame(0.1f, 0.1f, 0.1f, 1.0f);
    }
    const auto renderBuildStart = clock::now();

    inputs.game.render(inputs.drawableW, inputs.drawableH);
    const auto renderBuildEnd = clock::now();
    const auto submitStart = renderBuildEnd;
    out.serviceSnapshot = game::runtime::frame_observation::captureServiceSnapshot(inputs.services);

    game::runtime::frame_perf_capture::BackendFrameInputs backendPerfInputs;
    if (inputs.renderer) {
        inputs.renderer->endFrame();
        IRenderBackend::BackendFrameTimings backendTimings;
        backendPerfInputs.rendererHandlesPresentation = inputs.renderer->handlesPresentation();
        backendPerfInputs.hasBackendTimings = inputs.renderer->getLastFrameTimings(backendTimings);
        backendPerfInputs.backendTimings = backendTimings;
        if (!inputs.renderer->handlesPresentation() && inputs.swapBuffers) {
            const auto presentStart = clock::now();
            inputs.swapBuffers();
            const auto presentEnd = clock::now();
            backendPerfInputs.measuredPresentWaitMs =
                std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();
        }
        IRenderBackend::BackendFrameStats backendStats;
        backendPerfInputs.hasBackendStats = inputs.renderer->getLastFrameStats(backendStats);
        backendPerfInputs.backendStats = backendStats;
    } else if (inputs.swapBuffers) {
        const auto presentStart = clock::now();
        inputs.swapBuffers();
        const auto presentEnd = clock::now();
        backendPerfInputs.measuredPresentWaitMs =
            std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();
    }

    out.backendPerf =
        game::runtime::frame_perf_capture::resolveBackendFrameOutputs(backendPerfInputs);
    out.rendererHandlesPresentation = backendPerfInputs.rendererHandlesPresentation;

    const auto frameCpuEnd = clock::now();
    out.beginFrameMs =
        std::chrono::duration<double, std::milli>(renderBuildStart - beginFrameStart).count();
    out.renderBuildMs =
        std::chrono::duration<double, std::milli>(renderBuildEnd - renderBuildStart).count();
    out.submitRawMs =
        std::chrono::duration<double, std::milli>(frameCpuEnd - submitStart).count();
    out.frameCpuMs =
        std::chrono::duration<double, std::milli>(frameCpuEnd - frameCpuStart).count();
    return out;
}

} // namespace game::runtime::runner_frame_execution
