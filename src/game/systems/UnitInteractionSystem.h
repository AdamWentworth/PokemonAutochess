// src/game/systems/UnitInteractionSystem.h
#pragma once

#include "engine/render/Camera3D.h"
#include "game/GameWorld.h"
#include "engine/core/Updatable.h"
#include "engine/input/InputEvent.h"
#include "BenchSystem.h"

#include <glm/glm.hpp>

class UnitInteractionSystem : public Updatable {
public:
    UnitInteractionSystem(Camera3D* camera, GameWorld* world, unsigned int screenW, unsigned int screenH);

    // Engine-owned input boundary (SDL-free)
    void handleInput(const InputEvent& event);

    void update(float deltaTime) override;

    // Helpers for unit dragging interaction
    void onMouseButtonDown(int x, int y);
    void onMouseMotion(int x, int y);

private:
    glm::vec3 screenToWorld(int mouseX, int mouseY) const;
    bool isInBenchZone(const glm::vec3& pos) const;
    bool isInBoardZone(const glm::vec3& pos) const;

private:
    Camera3D* camera = nullptr;
    GameWorld* gameWorld = nullptr;

    bool draggingUnit = false;
    int draggedIndex = -1;
    bool draggingFromBench = false;

    float pickRadius = 0.7f;
    float cellSize = 1.2f;

    unsigned int screenW = 0;
    unsigned int screenH = 0;

    BenchSystem benchSystem;
};
