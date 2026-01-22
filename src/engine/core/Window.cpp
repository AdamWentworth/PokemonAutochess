// Window.cpp

#include "engine/core/Window.h"

#include <iostream>
#include <cstdlib>

Window::Window(const std::string& title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        std::exit(EXIT_FAILURE);
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    // explicit defaults (consistency)
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // allow HiDPI + resizing (click scaling handled in Application)
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;

    window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        flags
    );

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << "\n";
        std::exit(EXIT_FAILURE);
    }

    context = SDL_GL_CreateContext(window);
    if (!context) {
        std::cerr << "OpenGL context creation failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        window = nullptr;
        std::exit(EXIT_FAILURE);
    }

    SDL_GL_MakeCurrent(window, context);

    // VSync; if you want raw FPS for profiling, set to 0 temporarily.
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
