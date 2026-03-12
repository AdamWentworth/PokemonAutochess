#pragma once

#include <SDL2/SDL.h>

#include "engine/input/InputEvent.h"

namespace game::runtime::sdl_input {

struct TranslationContext {
    float mouseScaleX = 1.0f;
    float mouseScaleY = 1.0f;
    int windowW = 0;
    int windowH = 0;
    int drawableW = 0;
    int drawableH = 0;
};

InputEvent::MouseButton mapMouseButton(int sdlButton);

bool isResizeWindowEvent(const SDL_Event& sdl);

bool translateEvent(const SDL_Event& sdl,
                    const TranslationContext& context,
                    InputEvent& out);

} // namespace game::runtime::sdl_input
