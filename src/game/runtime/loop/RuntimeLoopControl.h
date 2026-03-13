#pragma once

#include "game/runtime/AutoQuitPolicy.h"

#include <SDL2/SDL.h>

#include <string>
#include <string_view>

namespace game::runtime::loop_control {

struct State {
    bool running = true;
    std::string stopReason;
    int renderedFrames = 0;
    double elapsedSeconds = 0.0;
};

bool isRunning(const State& state);

void requestStop(State& state, std::string_view reason);

void handleSdlQuitEvent(const SDL_Event& event, State& state);

void notePresentedFrame(State& state, double frameDt);

bool applyAutoQuit(const auto_quit::Policy& policy, State& state);

std::string effectiveStopReason(const State& state);

} // namespace game::runtime::loop_control
