#pragma once

#include "engine/core/EngineServices.h"
#include "game/runtime/loop/RuntimeFixedStepPhase.h"
#include "game/runtime/loop/RuntimeFrameObservation.h"
#include "game/runtime/loop/RuntimeFramePerfCapture.h"
#include "game/runtime/loop/RuntimePerfAccumulator.h"

#include <cstdint>
#include <iosfwd>

namespace game::runtime::runner_frame_diagnostics {

struct State {
    game::runtime::perf_accum::RollingAccumulator perfAccumulator;
    std::uint32_t previousGrowlRingCount = 0u;
    EngineTerminalLogMode previousTerminalLogMode = EngineTerminalLogMode::Performance;
};

struct Inputs {
    double frameDt = 0.0;
    double frameCpuMs = 0.0;
    double beginFrameMs = 0.0;
    double renderBuildMs = 0.0;
    double submitRawMs = 0.0;
    bool rendererHandlesPresentation = false;
    game::runtime::fixed_step_phase::Result fixedPhase{};
    game::runtime::frame_observation::ServiceSnapshot serviceSnapshot{};
    game::runtime::frame_perf_capture::BackendFrameOutputs backendPerf{};
};

State makeInitialState(const EngineServices& services);

void observeAndEmit(State& state,
                    EngineServices& services,
                    const Inputs& inputs,
                    std::ostream& out);

} // namespace game::runtime::runner_frame_diagnostics
