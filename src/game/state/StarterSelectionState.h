// StarterSelectionState.h
#pragma once

#include "../GameState.h"
#include <SDL2/SDL.h>
#include <sol/sol.hpp>
#include "../LuaScript.h"
#include "../../game/systems/CardSystem.h"

enum class StarterPokemon {
    None,
    Bulbasaur,
    Charmander,
    Squirtle
};

class GameStateManager;
class GameWorld;

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

    LuaScript script;
    CardSystem cardSystem;

    bool isPointInRect(int x, int y, const SDL_Rect& rect) const;
};
