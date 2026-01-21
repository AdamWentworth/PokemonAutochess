// src/game/input/SDLInputAdapter.h
#pragma once

#include <SDL2/SDL.h>

#include "engine/input/InputEvent.h"

/*
    SDLInputAdapter:
    - Temporary adapter to keep existing game code SDL-based while the
      engine/game boundary becomes SDL-free.
    - Converts InputEvent -> SDL_Event for existing systems.
*/
namespace SDLInputAdapter {
    // Returns true if conversion produced a meaningful SDL_Event
    bool toSDLEvent(const InputEvent& in, SDL_Event& out);
}
