// src/game/systems/UnitInteractionSystem.cpp
#include "UnitInteractionSystem.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/common.hpp>
#include <iostream>
#include <limits>

UnitInteractionSystem::UnitInteractionSystem(Camera3D* cam, GameWorld* world, unsigned int w, unsigned int h)
    : camera(cam), gameWorld(world), screenW(w), screenH(h), benchSystem(1.2f) {
    cellSize = 1.2f;

    // Input is routed through GameApp via InputEvent (engine-owned).
}

// Convert screen coordinates to world coordinates.
glm::vec3 UnitInteractionSystem::screenToWorld(int mouseX, int mouseY) const {
    glm::vec4 viewport(0.0f, 0.0f, static_cast<float>(screenW), static_cast<float>(screenH));
    float winX = static_cast<float>(mouseX);
    float winY = static_cast<float>(screenH - mouseY);

    glm::vec3 nearPt = glm::unProject(
        glm::vec3(winX, winY, 0.0f),
        camera->getViewMatrix(),
        camera->getProjectionMatrix(),
        viewport);

    glm::vec3 farPt = glm::unProject(
        glm::vec3(winX, winY, 1.0f),
        camera->getViewMatrix(),
        camera->getProjectionMatrix(),
        viewport);

    glm::vec3 dir = glm::normalize(farPt - nearPt);
    float t = -nearPt.y / dir.y;
    return nearPt + t * dir;
}

bool UnitInteractionSystem::isInBenchZone(const glm::vec3& pos) const {
    return benchSystem.isInBenchZone(pos);
}

bool UnitInteractionSystem::isInBoardZone(const glm::vec3& pos) const {
    return pos.z >= (cellSize * 0.5f) && pos.z <= (cellSize * 3.5f);
}

void UnitInteractionSystem::onMouseButtonDown(int x, int y) {
    glm::vec3 worldPos = screenToWorld(x, y);
    worldPos.y = 0.0f;

    if (!draggingUnit) {
        // PICKUP
        float best = std::numeric_limits<float>::max();
        int idx = -1;
        draggingFromBench = false;

        auto& board = gameWorld->getPokemons();
        for (int i = 0; i < static_cast<int>(board.size()); ++i) {
            if (board[i].side != PokemonSide::Player) continue;
            float d = glm::distance(worldPos, board[i].position);
            if (d < best) {
                best = d;
                idx = i;
                draggingFromBench = false;
            }
        }

        auto& bench = gameWorld->getBenchPokemons();
        for (int i = 0; i < static_cast<int>(bench.size()); ++i) {
            float d = glm::distance(worldPos, bench[i].position);
            if (d < best) {
                best = d;
                idx = i;
                draggingFromBench = true;
            }
        }

        if (idx >= 0 && best <= pickRadius) {
            draggingUnit = true;
            draggedIndex = idx;
            std::cout << "[UnitInteraction] Picked up index " << idx
                      << (draggingFromBench ? " from bench\n" : " from board\n");
        }
    } else {
        // DROP
        glm::vec3 snap = worldPos;
        bool toBench = isInBenchZone(worldPos);
        bool toBoard = isInBoardZone(worldPos);

        if (toBench) {
            snap = benchSystem.getSnappedBenchPosition(worldPos);
        } else if (toBoard) {
            // Snapping math handled during onMouseMotion; you can add a final snap here if you want.
        }

        if (toBench && !draggingFromBench) {
            auto& board = gameWorld->getPokemons();
            auto unit = board[draggedIndex];
            board.erase(board.begin() + draggedIndex);
            unit.position = snap;
            gameWorld->getBenchPokemons().push_back(unit);
            std::cout << "[UnitInteraction] Moved to bench\n";
        } else if (toBoard && draggingFromBench) {
            auto& bench = gameWorld->getBenchPokemons();
            auto unit = bench[draggedIndex];
            bench.erase(bench.begin() + draggedIndex);
            unit.position = snap;
            gameWorld->getPokemons().push_back(unit);
            std::cout << "[UnitInteraction] Moved to board\n";
        }

        // reset drag
        draggingUnit = false;
        draggedIndex = -1;
        draggingFromBench = false;
    }
}

void UnitInteractionSystem::onMouseMotion(int x, int y) {
    if (draggingUnit && draggedIndex >= 0) {
        glm::vec3 rawPos = screenToWorld(x, y);
        glm::vec3 snappedPos = rawPos;

        if (isInBenchZone(rawPos)) {
            snappedPos = benchSystem.getSnappedBenchPosition(rawPos);
        } else {
            float boardOriginX = -((8 * cellSize) / 2.0f) + cellSize * 0.5f;
            float boardOriginZ = cellSize * 0.5f;

            int col = static_cast<int>(std::round((rawPos.x - boardOriginX) / cellSize));
            int row = static_cast<int>(std::round((rawPos.z - boardOriginZ) / cellSize));

            col = glm::clamp(col, 0, 7);
            row = glm::clamp(row, 0, 3);

            snappedPos.x = boardOriginX + col * cellSize;
            snappedPos.z = boardOriginZ + row * cellSize;
            snappedPos.y = 0.0f;
        }

        if (draggingFromBench) {
            gameWorld->getBenchPokemons()[draggedIndex].position = snappedPos;
        } else {
            gameWorld->getPokemons()[draggedIndex].position = snappedPos;
        }
    }
}

void UnitInteractionSystem::handleInput(const InputEvent& event) {
    switch (event.type) {
        case InputEvent::Type::MouseDown:
            // Only left-click starts pickup/drop in this system.
            if (event.mouseButtonId == InputEvent::MouseButton::Left) {
                onMouseButtonDown(event.mouseX, event.mouseY);
            }
            break;
        case InputEvent::Type::MouseMove:
            onMouseMotion(event.mouseX, event.mouseY);
            break;
        default:
            break;
    }
}

void UnitInteractionSystem::update(float deltaTime) {
    (void)deltaTime;
    // no-op for now
}
