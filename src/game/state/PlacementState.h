// PlacementState.h

#pragma once

#include "game/GameState.h"
#include "game/GameWorld.h"
#include <string>
#include <memory>

class GameStateManager;
class GameWorld;
class TextRenderer;

class PlacementState : public GameState {
public:
    PlacementState(GameStateManager* manager, GameWorld* world, const std::string& starterName);
    ~PlacementState() override;

    void onEnter() override;
    void onExit() override;
    void handleInput(const InputEvent& event) override;
    void update(float deltaTime) override;
    void render() override;

private:
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    std::string starterName;
    float timer = 5.0f;
    bool placementDone = false;

    std::unique_ptr<TextRenderer> textRenderer;

    bool isStarterOnBoard() const;
    void moveStarterToBoard();
    bool isValidGridPosition(const glm::vec3& position) const;
    void moveStarterToValidGridPosition();
    void placeOnValidGridPosition(PokemonInstance& starter);
};
