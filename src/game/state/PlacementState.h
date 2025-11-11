// PlacementState.h

#pragma once

#include "../GameState.h"
#include "../GameWorld.h"
#include <SDL2/SDL.h>
#include <string>

class GameStateManager;
class GameWorld;

class PlacementState : public GameState {
public:
    PlacementState(GameStateManager* manager, GameWorld* world, const std::string& starterName);
    ~PlacementState();

    void onEnter() override;
    void onExit() override;
    void handleInput(SDL_Event& event) override;
    void update(float deltaTime) override;
    void render() override;

private:
    GameStateManager* stateManager;
    GameWorld* gameWorld;
    std::string starterName;
    float timer;
    bool placementDone;

    bool isStarterOnBoard() const;
    void moveStarterToBoard();
    bool isValidGridPosition(const glm::vec3& position) const;
    void moveStarterToValidGridPosition();
    void placeOnValidGridPosition(PokemonInstance& starter);
};