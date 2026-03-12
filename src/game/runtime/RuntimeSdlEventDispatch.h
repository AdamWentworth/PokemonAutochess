#pragma once

#include "engine/input/InputEvent.h"
#include "game/runtime/RuntimeLoopControl.h"
#include "game/runtime/RuntimeSdlInput.h"

#include <SDL2/SDL.h>

#include <functional>

namespace game::runtime::sdl_event_dispatch {

struct Callbacks {
    std::function<void()> onResize;
    std::function<void(const InputEvent&)> onInputEvent;
    std::function<game::runtime::sdl_input::TranslationContext()> makeTranslationContext;
};

void dispatch(const SDL_Event& event,
              game::runtime::loop_control::State& loopState,
              const Callbacks& callbacks);

} // namespace game::runtime::sdl_event_dispatch
