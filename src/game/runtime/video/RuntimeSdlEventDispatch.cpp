#include "game/runtime/video/RuntimeSdlEventDispatch.h"

namespace game::runtime::sdl_event_dispatch {

void dispatch(const SDL_Event& event,
              game::runtime::loop_control::State& loopState,
              const Callbacks& callbacks) {
    game::runtime::loop_control::handleSdlQuitEvent(event, loopState);

    if (game::runtime::sdl_input::isResizeWindowEvent(event) && callbacks.onResize) {
        callbacks.onResize();
    }

    if (!callbacks.makeTranslationContext || !callbacks.onInputEvent) {
        return;
    }

    InputEvent inputEvent;
    const auto translationContext = callbacks.makeTranslationContext();
    if (game::runtime::sdl_input::translateEvent(event, translationContext, inputEvent)) {
        callbacks.onInputEvent(inputEvent);
    }
}

} // namespace game::runtime::sdl_event_dispatch

