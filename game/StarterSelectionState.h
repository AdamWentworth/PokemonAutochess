// StarterSelectionState.h
#pragma once
#include "GameState.h"
#include <SDL2/SDL.h>

// Forward-declare GameStateManager (to store a pointer to it):
class GameStateManager;

enum class StarterPokemon {
    None,
    Bulbasaur,
    Charmander,
    Squirtle
};

class StarterSelectionState : public GameState {
public:
    // Ctor matches the .cpp signature: pass in a pointer to the manager
    StarterSelectionState(GameStateManager* manager);
    ~StarterSelectionState();

    void onEnter() override;
    void onExit() override;
    void handleInput(SDL_Event& event) override;
    void update(float deltaTime) override;
    void render() override;

private:
    StarterPokemon selectedStarter;

    SDL_Rect bulbasaurRect;
    SDL_Rect charmanderRect;
    SDL_Rect squirtleRect;

    // We only store a pointer, so forward declaration is enough in the .h
    GameStateManager* stateManager;

    bool isPointInRect(int x, int y, const SDL_Rect& rect) {
        return (x >= rect.x && x <= rect.x + rect.w &&
                y >= rect.y && y <= rect.y + rect.h);
    }
};
