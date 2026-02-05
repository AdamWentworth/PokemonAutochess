// src/engine/core/Window.cpp

#include "engine/core/Window.h"

#include <SDL2/SDL.h>

#include <iostream>
#include <stdexcept>
#include <string>

Window::Window(const std::string& title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        const std::string msg = std::string("SDL_Init failed: ") + SDL_GetError();
        std::cerr << msg << "\n";
        throw std::runtime_error(msg);
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;

    window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        flags
    );

    if (!window) {
        const std::string msg = std::string("Window creation failed: ") + SDL_GetError();
        std::cerr << msg << "\n";
        SDL_Quit();
        throw std::runtime_error(msg);
    }

    context = SDL_GL_CreateContext(window);
    if (!context) {
        const std::string msg = std::string("OpenGL context creation failed: ") + SDL_GetError();
        std::cerr << msg << "\n";
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        throw std::runtime_error(msg);
    }

    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
}

Window::~Window() {
    if (context) {
        SDL_GL_DeleteContext(context);
        context = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    // IMPORTANT: do NOT call SDL_Quit() here.
    // Application controls shutdown ordering.
}

void Window::setTitle(const std::string& title) {
    if (window) SDL_SetWindowTitle(window, title.c_str());
}

void Window::swapBuffers() {
    if (window) SDL_GL_SwapWindow(window);
}
