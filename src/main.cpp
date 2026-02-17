// main.cpp
#define SDL_MAIN_HANDLED

#include "game/runtime/GameRunner.h"

#if defined(_WIN32)
extern "C" {
// Hint laptop hybrid-GPU drivers to prefer the discrete GPU for this process.
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main() {
    return game::runGame();
}

