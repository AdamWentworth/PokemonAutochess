// StarterSelectionState.h
#pragma once
#include "../GameState.h"
#include <SDL2/SDL.h>
#include <iostream>

class GameStateManager;
class GameWorld; // Forward declaration

enum class StarterPokemon {
    None,
    Bulbasaur,
    Charmander,
    Squirtle
};

class StarterSelectionState : public GameState {
public:
    StarterSelectionState(GameStateManager* manager, GameWorld* world);
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

    GameStateManager* stateManager;
    GameWorld* gameWorld; // Stored pointer to the game world

    bool isPointInRect(int x, int y, const SDL_Rect& rect) {
        return (x >= rect.x && x <= rect.x + rect.w &&
                y >= rect.y && y <= rect.y + rect.h);
    }
};
