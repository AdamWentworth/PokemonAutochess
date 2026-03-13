#include <string>

#include <SDL2/SDL.h>

#include "game/runtime/loop/RuntimeLoopControl.h"

bool test_runtime_loop_control_contract(std::string& outFail) {
    using game::runtime::auto_quit::Policy;
    using game::runtime::loop_control::State;

    {
        State state;
        game::runtime::loop_control::requestStop(state, "manual stop");
        game::runtime::loop_control::requestStop(state, "later stop");
        if (state.running ||
            state.stopReason != "manual stop" ||
            game::runtime::loop_control::isRunning(state)) {
            outFail = "requestStop should stop the loop and preserve the first stop reason.";
            return false;
        }
    }

    {
        State state;
        SDL_Event quitEvent{};
        quitEvent.type = SDL_QUIT;
        game::runtime::loop_control::handleSdlQuitEvent(quitEvent, state);
        if (state.running || state.stopReason != "SDL_QUIT event") {
            outFail = "handleSdlQuitEvent should stop the loop on SDL_QUIT.";
            return false;
        }
    }

    {
        State state;
        game::runtime::loop_control::notePresentedFrame(state, 0.25);
        game::runtime::loop_control::notePresentedFrame(state, 0.50);
        if (state.renderedFrames != 2 || state.elapsedSeconds < 0.74 || state.elapsedSeconds > 0.76) {
            outFail = "notePresentedFrame should accumulate frame count and elapsed time.";
            return false;
        }
    }

    {
        State state;
        Policy policy;
        policy.maxFrames = 2;
        game::runtime::loop_control::notePresentedFrame(state, 0.2);
        if (game::runtime::loop_control::applyAutoQuit(policy, state)) {
            outFail = "applyAutoQuit should wait until the configured threshold is reached.";
            return false;
        }
        game::runtime::loop_control::notePresentedFrame(state, 0.2);
        if (!game::runtime::loop_control::applyAutoQuit(policy, state) ||
            state.stopReason != "PAC_AUTO_QUIT policy reached") {
            outFail = "applyAutoQuit should stop the loop when the configured frame/second threshold is met.";
            return false;
        }
    }

    {
        State state;
        if (game::runtime::loop_control::effectiveStopReason(state) != "main loop ended") {
            outFail = "effectiveStopReason should provide a fallback when the loop exits without an explicit reason.";
            return false;
        }
    }

    return true;
}

