#include <string>

#include <SDL2/SDL.h>

#include "engine/input/InputEvent.h"
#include "game/runtime/video/RuntimeSdlEventDispatch.h"

bool test_runtime_sdl_event_dispatch_contract(std::string& outFail) {
    using game::runtime::loop_control::State;
    using game::runtime::sdl_event_dispatch::Callbacks;

    {
        State loopState;
        int resizeCount = 0;
        int inputCount = 0;
        game::runtime::sdl_input::TranslationContext context{1.0f, 1.0f, 640, 360, 640, 360};
        Callbacks callbacks;
        callbacks.onResize = [&]() {
            ++resizeCount;
            context.windowW = 1280;
            context.windowH = 720;
            context.drawableW = 1920;
            context.drawableH = 1080;
        };
        callbacks.onInputEvent = [&](const InputEvent& event) {
            ++inputCount;
            if (event.type != InputEvent::Type::Resize ||
                event.windowW != 1280 ||
                event.windowH != 720 ||
                event.drawableW != 1920 ||
                event.drawableH != 1080) {
                outFail = "dispatch should translate resize events using the updated window/drawable context after the resize callback runs.";
            }
        };
        callbacks.makeTranslationContext = [&]() { return context; };

        SDL_Event resizeEvent{};
        resizeEvent.type = SDL_WINDOWEVENT;
        resizeEvent.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
        game::runtime::sdl_event_dispatch::dispatch(resizeEvent, loopState, callbacks);
        if (!outFail.empty()) {
            return false;
        }
        if (resizeCount != 1 || inputCount != 1 || !loopState.running) {
            outFail = "dispatch should run the resize callback, forward the translated event, and leave the loop running.";
            return false;
        }
    }

    {
        State loopState;
        bool sawQuitEvent = false;
        Callbacks callbacks;
        callbacks.onInputEvent = [&](const InputEvent& event) {
            sawQuitEvent = event.type == InputEvent::Type::Quit;
        };
        callbacks.makeTranslationContext = []() {
            return game::runtime::sdl_input::TranslationContext{};
        };

        SDL_Event quitEvent{};
        quitEvent.type = SDL_QUIT;
        game::runtime::sdl_event_dispatch::dispatch(quitEvent, loopState, callbacks);
        if (loopState.running || loopState.stopReason != "SDL_QUIT event" || !sawQuitEvent) {
            outFail = "dispatch should stop the loop on SDL_QUIT and still forward the translated quit input event.";
            return false;
        }
    }

    return true;
}

