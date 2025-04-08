// StarterSelectionState.h
#pragma once
#include "../GameState.h"
#include <SDL2/SDL.h>
#include "../../engine/ui/Card.h"
#include <sol/sol.hpp>
#include "../LuaScript.h"

class GameStateManager;
class GameWorld;

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
    GameStateManager* stateManager;
    GameWorld* gameWorld;
    StarterPokemon selectedStarter;

    SDL_Rect bulbasaurRect;
    SDL_Rect charmanderRect;
    SDL_Rect squirtleRect;

    LuaScript script;

    bool isPointInRect(int x, int y, const SDL_Rect& rect) const;
};