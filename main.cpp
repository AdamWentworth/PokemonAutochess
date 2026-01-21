// main.cpp
#define SDL_MAIN_HANDLED

#include "engine/core/Application.h"
#include "game/GameApp.h"

int main() {
    Application app;
    GameApp game;

    app.run(game);
    return 0;
}