#include "game/runtime/loop/RuntimeLoopControl.h"

namespace game::runtime::loop_control {

bool isRunning(const State& state) {
    return state.running;
}

void requestStop(State& state, std::string_view reason) {
    state.running = false;
    if (state.stopReason.empty()) {
        state.stopReason.assign(reason.begin(), reason.end());
    }
}

void handleSdlQuitEvent(const SDL_Event& event, State& state) {
    if (event.type == SDL_QUIT) {
        requestStop(state, "SDL_QUIT event");
    }
}

void notePresentedFrame(State& state, double frameDt) {
    ++state.renderedFrames;
    state.elapsedSeconds += frameDt;
}

bool applyAutoQuit(const auto_quit::Policy& policy, State& state) {
    if (!state.running) return false;
    if (!game::runtime::auto_quit::shouldTrigger(policy, state.elapsedSeconds, state.renderedFrames)) {
        return false;
    }
    requestStop(state, "PAC_AUTO_QUIT policy reached");
    return true;
}

std::string effectiveStopReason(const State& state) {
    return state.stopReason.empty() ? "main loop ended" : state.stopReason;
}

} // namespace game::runtime::loop_control

