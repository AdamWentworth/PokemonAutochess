#pragma once

struct GameContext;
class GameLoop;

namespace game::runtime::loop_control {
struct State;
}

namespace game::runtime::window_presentation {
class WindowPresentationController;
}

namespace game::runtime::runner_event_pump {

void pumpWindowEvents(
    game::runtime::window_presentation::WindowPresentationController& presentation,
    GameContext& ctx,
    GameLoop& game,
    game::runtime::loop_control::State& loopState);

} // namespace game::runtime::runner_event_pump
