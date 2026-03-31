#include "game/runtime/loop/RuntimeGameRunnerEventPump.h"

#include "engine/core/GameContext.h"
#include "engine/core/GameLoop.h"
#include "game/runtime/loop/RuntimeLoopControl.h"
#include "game/runtime/video/RuntimeSdlEventDispatch.h"
#include "game/runtime/video/RuntimeWindowPresentationController.h"

#include <SDL2/SDL.h>

namespace game::runtime::runner_event_pump {

void pumpWindowEvents(
    game::runtime::window_presentation::WindowPresentationController& presentation,
    GameContext& ctx,
    GameLoop& game,
    game::runtime::loop_control::State& loopState) {
    SDL_Event sdlEvent;
    while (SDL_PollEvent(&sdlEvent)) {
        game::runtime::sdl_event_dispatch::Callbacks eventCallbacks;
        eventCallbacks.onResize = [&presentation, &ctx]() {
            presentation.syncVideoModeState();
            presentation.noteCurrentWindowModeChanged(false);
            ctx.drawableW = presentation.drawableWidth();
            ctx.drawableH = presentation.drawableHeight();
        };
        eventCallbacks.onInputEvent = [&game](const InputEvent& event) {
            game.handleEvent(event);
        };
        eventCallbacks.makeTranslationContext = [&presentation]() {
            game::runtime::sdl_input::TranslationContext inputContext;
            inputContext.mouseScaleX = presentation.mouseScaleX();
            inputContext.mouseScaleY = presentation.mouseScaleY();
            inputContext.windowW = presentation.windowWidth();
            inputContext.windowH = presentation.windowHeight();
            inputContext.drawableW = presentation.drawableWidth();
            inputContext.drawableH = presentation.drawableHeight();
            return inputContext;
        };
        game::runtime::sdl_event_dispatch::dispatch(sdlEvent, loopState, eventCallbacks);
    }
}

} // namespace game::runtime::runner_event_pump
