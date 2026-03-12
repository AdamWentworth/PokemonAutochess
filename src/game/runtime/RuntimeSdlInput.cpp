#include "game/runtime/RuntimeSdlInput.h"

#include <cmath>

#include "engine/input/SdlKeyMap.h"

namespace {

int scaleMouseAxis(int value, float scale) {
    return static_cast<int>(std::lround(static_cast<float>(value) * scale));
}

} // namespace

namespace game::runtime::sdl_input {

InputEvent::MouseButton mapMouseButton(int sdlButton) {
    switch (sdlButton) {
        case SDL_BUTTON_LEFT: return InputEvent::MouseButton::Left;
        case SDL_BUTTON_MIDDLE: return InputEvent::MouseButton::Middle;
        case SDL_BUTTON_RIGHT: return InputEvent::MouseButton::Right;
        case SDL_BUTTON_X1: return InputEvent::MouseButton::X1;
        case SDL_BUTTON_X2: return InputEvent::MouseButton::X2;
        default:
            return InputEvent::MouseButton::Unknown;
    }
}

bool isResizeWindowEvent(const SDL_Event& sdl) {
    return sdl.type == SDL_WINDOWEVENT &&
           (sdl.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
            sdl.window.event == SDL_WINDOWEVENT_RESIZED);
}

bool translateEvent(const SDL_Event& sdl,
                    const TranslationContext& context,
                    InputEvent& out) {
    switch (sdl.type) {
        case SDL_QUIT:
            out = InputEvent::QuitEvent();
            return true;

        case SDL_WINDOWEVENT:
            if (!isResizeWindowEvent(sdl)) {
                return false;
            }
            out = InputEvent::ResizeEvent(
                context.windowW,
                context.windowH,
                context.drawableW,
                context.drawableH);
            return true;

        case SDL_KEYDOWN:
            out = InputEvent::KeyDownEvent(
                engine::input::mapSdlKeyToEngineKey(static_cast<int>(sdl.key.keysym.sym)),
                sdl.key.repeat != 0);
            return true;

        case SDL_KEYUP:
            out = InputEvent::KeyUpEvent(
                engine::input::mapSdlKeyToEngineKey(static_cast<int>(sdl.key.keysym.sym)));
            return true;

        case SDL_MOUSEMOTION:
            out = InputEvent::MouseMoveEvent(
                scaleMouseAxis(sdl.motion.x, context.mouseScaleX),
                scaleMouseAxis(sdl.motion.y, context.mouseScaleY));
            return true;

        case SDL_MOUSEBUTTONDOWN:
            out = InputEvent::MouseDownEvent(
                scaleMouseAxis(sdl.button.x, context.mouseScaleX),
                scaleMouseAxis(sdl.button.y, context.mouseScaleY),
                mapMouseButton(static_cast<int>(sdl.button.button)));
            return true;

        case SDL_MOUSEBUTTONUP:
            out = InputEvent::MouseUpEvent(
                scaleMouseAxis(sdl.button.x, context.mouseScaleX),
                scaleMouseAxis(sdl.button.y, context.mouseScaleY),
                mapMouseButton(static_cast<int>(sdl.button.button)));
            return true;

        case SDL_MOUSEWHEEL:
            out = InputEvent::MouseWheelEvent(
                static_cast<int>(sdl.wheel.x),
                static_cast<int>(sdl.wheel.y));
            return true;

        default:
            return false;
    }
}

} // namespace game::runtime::sdl_input
