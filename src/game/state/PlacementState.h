#pragma once

#include "game/GameState.h"
#include "game/GameConfig.h"
#include <string>

#include <glm/glm.hpp>

class GameStateManager;
class GameWorld;
struct GameServices;
struct PokemonInstance;

// Placement state: user drags starter onto board, then transitions to combat.
// Supports incremental GameServices injection (services optional).
class PlacementState : public GameState {
public:
    PlacementState(GameStateManager* manager, GameWorld* world, const std::string& starterName);
    PlacementState(GameStateManager* manager, GameWorld* world, GameServices& services, const std::string& starterName);
    ~PlacementState() override;

    void onEnter() override;
    void onExit() override;
    void handleInput(const InputEvent& event) override;
    void update(float deltaTime) override;
    void render() override;

private:
    const GameConfigData& cfg() const;

    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    GameServices* services = nullptr;
    GameConfigData fallbackConfig;

    std::string starterName;
    float timer = 5.0f;
    bool placementDone = false;

    bool isStarterOnBoard() const;
    void moveStarterToBoard();

    bool isValidGridPosition(const glm::vec3& position) const;
    void moveStarterToValidGridPosition();
    void placeOnValidGridPosition(PokemonInstance& starter);
};
