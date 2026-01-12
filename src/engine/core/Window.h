// Window.h

#ifndef WINDOW_H
#define WINDOW_H

#include <SDL2/SDL.h>
#include <string>

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    SDL_Window* getSDLWindow() const { return window; }
    SDL_GLContext getContext() const { return context; }

private:
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
};

#endif // WINDOW_H
