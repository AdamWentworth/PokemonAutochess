// src/engine/input/InputEvent.h
#pragma once

#include <cstdint>

/*
    InputEvent:
    - Engine-owned input/event type used at the engine/game boundary.
    - Keeps SDL types out of the public interface.
    - Only contains the data your game actually needs.
*/
struct InputEvent {
    enum class Type : std::uint8_t {
        Unknown = 0,

        Quit,
        Resize,

        KeyDown,
        KeyUp,

        MouseMove,
        MouseDown,
        MouseUp,

        MouseWheel
    };

    Type type = Type::Unknown;

    // --- Keyboard ---
    int key = 0;          // SDL_Keycode-compatible integer (for now)
    bool repeat = false;

    // --- Mouse ---
    int mouseX = 0;
    int mouseY = 0;
    int mouseButton = 0;  // SDL_BUTTON_* compatible integer (for now)

    // --- Wheel ---
    int wheelX = 0;
    int wheelY = 0;

    // --- Resize ---
    int windowW = 0;
    int windowH = 0;
    int drawableW = 0;
    int drawableH = 0;

    // Convenience constructors
    static InputEvent QuitEvent() {
        InputEvent e; e.type = Type::Quit; return e;
    }

    static InputEvent ResizeEvent(int wW, int wH, int dW, int dH) {
        InputEvent e;
        e.type = Type::Resize;
        e.windowW = wW; e.windowH = wH;
        e.drawableW = dW; e.drawableH = dH;
        return e;
    }

    static InputEvent KeyDownEvent(int keycode, bool isRepeat = false) {
        InputEvent e;
        e.type = Type::KeyDown;
        e.key = keycode;
        e.repeat = isRepeat;
        return e;
    }

    static InputEvent KeyUpEvent(int keycode) {
        InputEvent e;
        e.type = Type::KeyUp;
        e.key = keycode;
        return e;
    }

    static InputEvent MouseMoveEvent(int x, int y) {
        InputEvent e;
        e.type = Type::MouseMove;
        e.mouseX = x; e.mouseY = y;
        return e;
    }

    static InputEvent MouseDownEvent(int x, int y, int btn) {
        InputEvent e;
        e.type = Type::MouseDown;
        e.mouseX = x; e.mouseY = y;
        e.mouseButton = btn;
        return e;
    }

    static InputEvent MouseUpEvent(int x, int y, int btn) {
        InputEvent e;
        e.type = Type::MouseUp;
        e.mouseX = x; e.mouseY = y;
        e.mouseButton = btn;
        return e;
    }

    static InputEvent MouseWheelEvent(int x, int y) {
        InputEvent e;
        e.type = Type::MouseWheel;
        e.wheelX = x; e.wheelY = y;
        return e;
    }
};
