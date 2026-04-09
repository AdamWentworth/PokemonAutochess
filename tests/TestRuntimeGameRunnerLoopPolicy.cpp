#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "game/runtime/loop/RuntimeGameRunnerLoopPolicy.h"
#include "TestEnvVarUtils.h"

namespace {
using test::env_utils::ScopedEnvVar;
using test::env_utils::setEnvVar;

} // namespace

bool test_runtime_game_runner_loop_policy_contract(std::string& outFail) {
    using game::runtime::loop_control::State;
    using game::runtime::runner_loop_policy::Config;
    using game::runtime::runner_loop_policy::Clock;
    using game::runtime::runner_loop_policy::beginFrame;
    using game::runtime::runner_loop_policy::finishFrame;
    using game::runtime::runner_loop_policy::logExit;
    using game::runtime::runner_loop_policy::makeInitialState;
    using game::runtime::runner_loop_policy::readConfig;

    ScopedEnvVar maxTicksGuard("PAC_MAX_FIXED_TICKS_PER_FRAME");
    ScopedEnvVar autoSecondsGuard("PAC_AUTO_QUIT_SECONDS");
    ScopedEnvVar autoFramesGuard("PAC_AUTO_QUIT_FRAMES");

    if (!setEnvVar(maxTicksGuard.name.c_str(), "7") ||
        !setEnvVar(autoSecondsGuard.name.c_str(), "1.5") ||
        !setEnvVar(autoFramesGuard.name.c_str(), "120")) {
        outFail = "Failed to seed loop policy environment variables.";
        return false;
    }

    std::ostringstream out;
    std::ostringstream err;
    const Config config = readConfig(out, err);
    if (config.maxFixedTicksPerFrame != 7 ||
        std::fabs(config.autoQuit.maxSeconds - 1.5) > 0.0001 ||
        config.autoQuit.maxFrames != 120 ||
        out.str().find("Fixed tick budget: 7 ticks/frame") == std::string::npos ||
        out.str().find("Auto-quit policy enabled: seconds=1.5 frames=120") == std::string::npos ||
        !err.str().empty()) {
        outFail = "readConfig should read loop pacing configuration from environment and emit stable startup logs.";
        return false;
    }

    {
        State loopState;
        auto policyState = makeInitialState(
            Config{
                .maxFixedTicksPerFrame = 4,
                .autoQuit = {.maxFrames = 1},
            },
            Clock::now() - std::chrono::seconds(1));
        const auto frameStart = beginFrame(policyState);
        if (frameStart.frameDt < 0.249 || frameStart.frameDt > 0.251 ||
            std::fabs(game::runtime::runner_loop_policy::accumulator(policyState) - 0.25) > 0.001) {
            outFail = "beginFrame should clamp large frame deltas and accumulate the clamped value.";
            return false;
        }

        int frameCapCalls = 0;
        finishFrame(
            policyState,
            loopState,
            frameStart.frameDt,
            frameStart.frameStart,
            [&frameCapCalls](const auto&) { ++frameCapCalls; });
        if (loopState.running ||
            loopState.renderedFrames != 1 ||
            std::fabs(loopState.elapsedSeconds - frameStart.frameDt) > 0.0001 ||
            frameCapCalls != 0) {
            outFail = "finishFrame should note the presented frame, apply auto-quit, and skip frame capping once the loop stops.";
            return false;
        }

        std::ostringstream exitOut;
        logExit(loopState, exitOut);
        if (exitOut.str().find("PAC_AUTO_QUIT policy reached") == std::string::npos) {
            outFail = "logExit should report the effective loop stop reason.";
            return false;
        }
    }

    {
        State loopState;
        auto policyState = makeInitialState(Config{}, Clock::now());
        game::runtime::runner_loop_policy::setAccumulator(policyState, 0.75);
        if (std::fabs(game::runtime::runner_loop_policy::accumulator(policyState) - 0.75) > 0.0001) {
            outFail = "setAccumulator should preserve the residual accumulator for the next frame.";
            return false;
        }

        int frameCapCalls = 0;
        finishFrame(
            policyState,
            loopState,
            0.016,
            Clock::now(),
            [&frameCapCalls](const auto&) { ++frameCapCalls; });
        if (!loopState.running || frameCapCalls != 1) {
            outFail = "finishFrame should invoke frame capping when the loop remains active.";
            return false;
        }
    }

    return true;
}
