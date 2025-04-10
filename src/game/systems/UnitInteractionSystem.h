// UnitInteractionSystem.h

#pragma once

#include "../../engine/render/Camera3D.h"
#include "../../game/GameWorld.h"
#include "../../engine/core/IUpdatable.h"
#include "BenchSystem.h"
#include <SDL2/SDL.h>
#include <glm/glm.hpp>

class UnitInteractionSystem : public IUpdatable {
public:
    UnitInteractionSystem(Camera3D* camera, GameWorld* world, unsigned int screenW, unsigned int screenH);
    void handleEvent(const SDL_Event& event);
    void update(float deltaTime) override;

    // New event handling methods.
    void onMouseButtonDown(int x, int y);
    void onMouseMotion(int x, int y);

private:
    glm::vec3 screenToWorld(int mouseX, int mouseY) const;
    bool isInBenchZone(const glm::vec3& pos) const;
    bool isInBoardZone(const glm::vec3& pos) const;

    Camera3D* camera;
    GameWorld* gameWorld;

    bool draggingUnit = false;
    int draggedIndex = -1;
    bool draggingFromBench = false;

    float pickRadius = 0.7f;
    float cellSize = 1.2f;
    unsigned int screenW;
    unsigned int screenH;

    BenchSystem benchSystem;
};
