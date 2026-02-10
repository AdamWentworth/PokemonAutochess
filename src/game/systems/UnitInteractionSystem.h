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
    void setScreenSize(unsigned int width, unsigned int height);

    // Engine-owned input boundary (SDL-free)
    void handleInput(const InputEvent& event);

    void update(float deltaTime) override;

    // Helpers for unit dragging interaction
    void onMouseButtonDown(int x, int y);
    void onMouseMotion(int x, int y);

private:
    glm::vec3 screenToWorld(int mouseX, int mouseY) const;
    glm::vec3 snapBoardPosition(const glm::vec3& worldPos) const;
    bool findNearestAvailableBoardCell(const glm::vec3& worldPos, int ignoreIndex, glm::vec3& outPos) const;
    bool findNearestAvailableBenchSlot(const glm::vec3& worldPos, int ignoreIndex, glm::vec3& outPos) const;
    bool isInBenchZone(const glm::vec3& pos) const;
    bool isInBoardZone(const glm::vec3& pos) const;
    bool isInSellDropZoneScreen(int mouseX, int mouseY) const;
    bool isBoardCellOccupied(const glm::vec3& pos, int ignoreIndex) const;
    bool isBenchSlotOccupied(const glm::vec3& pos, int ignoreIndex) const;
    bool isSameCell(const glm::vec3& a, const glm::vec3& b, float eps = 0.01f) const;

private:
    Camera3D* camera = nullptr;
    GameWorld* gameWorld = nullptr;

    bool draggingUnit = false;
    int draggedIndex = -1;
    bool draggingFromBench = false;
    glm::vec3 dragStartPos{};

    float pickRadius = 0.7f;
    float cellSize = 1.2f;

    unsigned int screenW = 0;
    unsigned int screenH = 0;

    BenchSystem benchSystem;
};
