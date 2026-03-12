#include <string>

#include <SDL2/SDL.h>

#include "engine/input/InputEvent.h"
#include "game/runtime/RuntimeSdlInput.h"

bool test_runtime_sdl_input_contract(std::string& outFail) {
    using game::runtime::sdl_input::TranslationContext;
    using game::runtime::sdl_input::isResizeWindowEvent;
    using game::runtime::sdl_input::mapMouseButton;
    using game::runtime::sdl_input::translateEvent;

    if (mapMouseButton(SDL_BUTTON_LEFT) != InputEvent::MouseButton::Left ||
        mapMouseButton(255) != InputEvent::MouseButton::Unknown) {
        outFail = "mapMouseButton should translate known SDL buttons and reject unknown ones.";
        return false;
    }

    {
        SDL_Event sdl{};
        sdl.type = SDL_WINDOWEVENT;
        sdl.window.event = SDL_WINDOWEVENT_RESIZED;
        if (!isResizeWindowEvent(sdl)) {
            outFail = "isResizeWindowEvent should detect SDL resize notifications.";
            return false;
        }
        sdl.window.event = SDL_WINDOWEVENT_MOVED;
        if (isResizeWindowEvent(sdl)) {
            outFail = "isResizeWindowEvent should ignore unrelated window events.";
            return false;
        }
    }

    const TranslationContext context{1.5f, 2.0f, 1280, 720, 1920, 1440};

    {
        SDL_Event sdl{};
        sdl.type = SDL_QUIT;
        InputEvent out;
        if (!translateEvent(sdl, context, out) || out.type != InputEvent::Type::Quit) {
            outFail = "translateEvent should translate SDL_QUIT to an engine quit event.";
            return false;
        }
    }

    {
        SDL_Event sdl{};
        sdl.type = SDL_WINDOWEVENT;
        sdl.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
        InputEvent out;
        if (!translateEvent(sdl, context, out) ||
            out.type != InputEvent::Type::Resize ||
            out.windowW != 1280 ||
            out.windowH != 720 ||
            out.drawableW != 1920 ||
            out.drawableH != 1440) {
            outFail = "translateEvent should preserve window and drawable sizes in resize events.";
            return false;
        }
    }

    {
        SDL_Event sdl{};
        sdl.type = SDL_KEYDOWN;
        sdl.key.keysym.sym = SDLK_ESCAPE;
        sdl.key.repeat = 1;
        InputEvent out;
        if (!translateEvent(sdl, context, out) ||
            out.type != InputEvent::Type::KeyDown ||
            out.keyId != InputEvent::Key::Escape ||
            !out.repeat) {
            outFail = "translateEvent should translate SDL keydown events including repeat state.";
            return false;
        }
    }

    {
        SDL_Event sdl{};
        sdl.type = SDL_KEYUP;
        sdl.key.keysym.sym = SDLK_a;
        InputEvent out;
        if (!translateEvent(sdl, context, out) ||
            out.type != InputEvent::Type::KeyUp ||
            out.keyId != InputEvent::Key::A) {
            outFail = "translateEvent should translate SDL keyup events.";
            return false;
        }
    }

    {
        SDL_Event sdl{};
        sdl.type = SDL_MOUSEMOTION;
        sdl.motion.x = 11;
        sdl.motion.y = 7;
        InputEvent out;
        if (!translateEvent(sdl, context, out) ||
            out.type != InputEvent::Type::MouseMove ||
            out.mouseX != 17 ||
            out.mouseY != 14) {
            outFail = "translateEvent should scale mouse motion into drawable coordinates.";
            return false;
        }
    }

    {
        SDL_Event sdl{};
        sdl.type = SDL_MOUSEBUTTONDOWN;
        sdl.button.x = 20;
        sdl.button.y = 10;
        sdl.button.button = SDL_BUTTON_RIGHT;
        InputEvent out;
        if (!translateEvent(sdl, context, out) ||
            out.type != InputEvent::Type::MouseDown ||
            out.mouseX != 30 ||
            out.mouseY != 20 ||
            out.mouseButtonId != InputEvent::MouseButton::Right) {
            outFail = "translateEvent should scale mouse button presses and map the SDL mouse button.";
            return false;
        }
    }

    {
        SDL_Event sdl{};
        sdl.type = SDL_MOUSEBUTTONUP;
        sdl.button.x = 4;
        sdl.button.y = 9;
        sdl.button.button = SDL_BUTTON_X1;
        InputEvent out;
        if (!translateEvent(sdl, context, out) ||
            out.type != InputEvent::Type::MouseUp ||
            out.mouseX != 6 ||
            out.mouseY != 18 ||
            out.mouseButtonId != InputEvent::MouseButton::X1) {
            outFail = "translateEvent should scale mouse button releases.";
            return false;
        }
    }

    {
        SDL_Event sdl{};
        sdl.type = SDL_MOUSEWHEEL;
        sdl.wheel.x = -1;
        sdl.wheel.y = 2;
        InputEvent out;
        if (!translateEvent(sdl, context, out) ||
            out.type != InputEvent::Type::MouseWheel ||
            out.wheelX != -1 ||
            out.wheelY != 2) {
            outFail = "translateEvent should preserve SDL mouse wheel deltas.";
            return false;
        }
    }

    {
        SDL_Event sdl{};
        sdl.type = SDL_USEREVENT;
        InputEvent out;
        if (translateEvent(sdl, context, out)) {
            outFail = "translateEvent should ignore SDL event types the engine does not use.";
            return false;
        }
    }

    return true;
}
