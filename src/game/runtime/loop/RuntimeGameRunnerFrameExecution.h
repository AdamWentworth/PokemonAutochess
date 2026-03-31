#pragma once

#include "game/runtime/loop/RuntimeFixedStepPhase.h"
#include "game/runtime/loop/RuntimeFrameObservation.h"
#include "game/runtime/loop/RuntimeFramePerfCapture.h"

#include <functional>

struct EngineServices;
class GameLoop;
class IRenderBackend;

namespace game::runtime::runner_frame_execution {

struct Inputs {
    double accumulator = 0.0;
    double frameDt = 0.0;
    int maxFixedTicksPerFrame = 0;
    int drawableW = 0;
    int drawableH = 0;
    EngineServices& services;
    GameLoop& game;
    IRenderBackend* renderer = nullptr;
    std::function<void()> swapBuffers;
};

struct Outputs {
    double accumulator = 0.0;
    game::runtime::fixed_step_phase::Result fixedPhase{};
    game::runtime::frame_observation::ServiceSnapshot serviceSnapshot{};
    game::runtime::frame_perf_capture::BackendFrameOutputs backendPerf{};
    bool rendererHandlesPresentation = false;
    double frameCpuMs = 0.0;
    double beginFrameMs = 0.0;
    double renderBuildMs = 0.0;
    double submitRawMs = 0.0;
};

Outputs execute(const Inputs& inputs);

} // namespace game::runtime::runner_frame_execution
