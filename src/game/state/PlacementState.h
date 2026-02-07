#pragma once

#include "game/GameState.h"
#include "game/GameServices.h"
#include <string>

#include <glm/glm.hpp>

class GameStateManager;
class GameWorld;
struct PokemonInstance;

// Placement state: user drags starter onto board, then transitions to combat.
class PlacementState : public GameState {
public:
    PlacementState(GameStateManager* manager, GameWorld* world, GameServices& services, const std::string& starterName);
    ~PlacementState() override;

    void onEnter() override;
    void onExit() override;
    void handleInput(const InputEvent& event) override;
    void update(float deltaTime) override;
    void render() override;

private:
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    GameServices& services;

    std::string starterName;
    float timer = 5.0f;
    bool placementDone = false;

    bool isStarterOnBoard() const;
    void moveStarterToBoard();

    bool isValidGridPosition(const glm::vec3& position) const;
    void moveStarterToValidGridPosition();
    void placeOnValidGridPosition(PokemonInstance& starter);
};
