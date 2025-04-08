// StarterSelectionState.h
#pragma once

#include "../GameState.h"
#include <SDL2/SDL.h>
#include <sol/sol.hpp>
#include "../LuaScript.h"

#include "StarterSelectionState.h"
#include "../../game/systems/CardSystem.h"

enum class StarterPokemon {
    None,
    Bulbasaur,
    Charmander,
    Squirtle
};

// Forward declarations
class GameStateManager;
class GameWorld;

/**
 * StarterSelectionState: a state that handles the user picking
 * their first starter Pokemon. Demonstrates usage of CardSystem.
 */
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

    // Our script that ties to "starter_selection.lua"
    LuaScript script;

    // The CardSystem that will handle our card creation, rendering, input
    CardSystem cardSystem;

    // If you still want to do direct bounding box checks, keep these rects:
    SDL_Rect bulbasaurRect;
    SDL_Rect charmanderRect;
    SDL_Rect squirtleRect;

    // Helper to detect clicks inside a rectangle
    bool isPointInRect(int x, int y, const SDL_Rect& rect) const;
};
