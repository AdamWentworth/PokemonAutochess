// src/game/input/SDLInputAdapter.cpp

#include "SDLInputAdapter.h"

#include <cstring>

namespace SDLInputAdapter {

bool toSDLEvent(const InputEvent& in, SDL_Event& out) {
    std::memset(&out, 0, sizeof(SDL_Event));

    switch (in.type) {
        case InputEvent::Type::Quit: {
            out.type = SDL_QUIT;
            return true;
        }

        case InputEvent::Type::Resize: {
            out.type = SDL_WINDOWEVENT;
            out.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
            out.window.data1 = in.windowW;
            out.window.data2 = in.windowH;
            return true;
        }

        case InputEvent::Type::KeyDown: {
            out.type = SDL_KEYDOWN;
            out.key.keysym.sym = (SDL_Keycode)in.key;
            out.key.repeat = in.repeat ? 1 : 0;
            return true;
        }

        case InputEvent::Type::KeyUp: {
            out.type = SDL_KEYUP;
            out.key.keysym.sym = (SDL_Keycode)in.key;
            out.key.repeat = 0;
            return true;
        }

        case InputEvent::Type::MouseMove: {
            out.type = SDL_MOUSEMOTION;
            out.motion.x = in.mouseX;
            out.motion.y = in.mouseY;
            return true;
        }

        case InputEvent::Type::MouseDown: {
            out.type = SDL_MOUSEBUTTONDOWN;
            out.button.button = (std::uint8_t)in.mouseButton;
            out.button.x = in.mouseX;
            out.button.y = in.mouseY;
            return true;
        }

        case InputEvent::Type::MouseUp: {
            out.type = SDL_MOUSEBUTTONUP;
            out.button.button = (std::uint8_t)in.mouseButton;
            out.button.x = in.mouseX;
            out.button.y = in.mouseY;
            return true;
        }

        case InputEvent::Type::MouseWheel: {
            out.type = SDL_MOUSEWHEEL;
            out.wheel.x = in.wheelX;
            out.wheel.y = in.wheelY;
            return true;
        }

        default:
            return false;
    }
}

} // namespace SDLInputAdapter
