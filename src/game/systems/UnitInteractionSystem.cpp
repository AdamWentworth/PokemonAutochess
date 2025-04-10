// UnitInteractionSystem.cpp

#include "UnitInteractionSystem.h"
#include "../../engine/events/EventManager.h"
#include "../../engine/events/Event.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/common.hpp>
#include <iostream>
#include <limits>

UnitInteractionSystem::UnitInteractionSystem(Camera3D* cam, GameWorld* world, unsigned int w, unsigned int h)
    : camera(cam), gameWorld(world), screenW(w), screenH(h), benchSystem(1.2f)
{
    cellSize = 1.2f;

    // Subscribe to MouseButtonDownEvent.
    EventManager::getInstance().subscribe(EventType::MouseButtonDown, 
        [this](const Event& e){
            const auto& mbe = static_cast<const MouseButtonDownEvent&>(e);
            onMouseButtonDown(mbe.getX(), mbe.getY());
        });
    EventManager::getInstance().subscribe(EventType::MouseMoved, 
        [this](const Event& e){
            const auto& mme = static_cast<const MouseMotionEvent&>(e);
            onMouseMotion(mme.getX(), mme.getY());
        });
}

// Convert screen coordinates to world coordinates.
glm::vec3 UnitInteractionSystem::screenToWorld(int mouseX, int mouseY) const {
    glm::vec4 viewport(0.0f, 0.0f, screenW, screenH);
    float winX = static_cast<float>(mouseX);
    float winY = static_cast<float>(screenH - mouseY);
    glm::vec3 near = glm::unProject(glm::vec3(winX, winY, 0.0f),
                                    camera->getViewMatrix(), camera->getProjectionMatrix(), viewport);
    glm::vec3 far = glm::unProject(glm::vec3(winX, winY, 1.0f),
                                   camera->getViewMatrix(), camera->getProjectionMatrix(), viewport);
    glm::vec3 dir = glm::normalize(far - near);
    float t = -near.y / dir.y;
    return near + t * dir;
}

bool UnitInteractionSystem::isInBenchZone(const glm::vec3& pos) const {
    return benchSystem.isInBenchZone(pos);
}

bool UnitInteractionSystem::isInBoardZone(const glm::vec3& pos) const {
    return pos.z >= (cellSize * 0.5f) && pos.z <= (cellSize * 3.5f);
}

// New event handling methods:
void UnitInteractionSystem::onMouseButtonDown(int x, int y) {
    glm::vec3 worldPos = screenToWorld(x,y);
    worldPos.y = 0.0f;

    if (!draggingUnit) {
        // *** PICKUP ***
        float best = std::numeric_limits<float>::max();
        int idx = -1;
        draggingFromBench = false;

        auto& board = gameWorld->getPokemons();
        for (int i = 0; i < (int)board.size(); ++i) {
            if (board[i].side != PokemonSide::Player) continue;
            float d = glm::distance(worldPos, board[i].position);
            if (d < best) { best = d; idx = i; draggingFromBench = false; }
        }
        auto& bench = gameWorld->getBenchPokemons();
        for (int i = 0; i < (int)bench.size(); ++i) {
            float d = glm::distance(worldPos, bench[i].position);
            if (d < best) { best = d; idx = i; draggingFromBench = true; }
        }

        if (idx >= 0 && best <= pickRadius) {
            draggingUnit = true;
            draggedIndex = idx;
            std::cout << "[UnitInteraction] Picked up index " << idx 
                      << (draggingFromBench ? " from bench\n" : " from board\n");
        }
    }
    else {
        // *** DROP ***
        glm::vec3 snap = worldPos;
        bool toBench = isInBenchZone(worldPos);
        bool toBoard = isInBoardZone(worldPos);

        if (toBench) {
            snap = benchSystem.getSnappedBenchPosition(worldPos);
        } else if (toBoard) {
            // same snapping math as before...
        }

        if (toBench && !draggingFromBench) {
            auto& board = gameWorld->getPokemons();
            auto unit = board[draggedIndex];
            board.erase(board.begin()+draggedIndex);
            unit.position = snap;
            gameWorld->getBenchPokemons().push_back(unit);
            std::cout<<"[UnitInteraction] Moved to bench\n";
        }
        else if (toBoard && draggingFromBench) {
            auto& bench = gameWorld->getBenchPokemons();
            auto unit = bench[draggedIndex];
            bench.erase(bench.begin()+draggedIndex);
            unit.position = snap;
            gameWorld->getPokemons().push_back(unit);
            std::cout<<"[UnitInteraction] Moved to board\n";
        }
        // reset
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
        }
        else {
            gameWorld->getPokemons()[draggedIndex].position = snappedPos;
        }
    }
}

// For backwards compatibility, you may leave handleEvent empty:
void UnitInteractionSystem::handleEvent(const SDL_Event& event) {
    // Now events are handled via the event system.
}

void UnitInteractionSystem::update(float deltaTime) {
    (void)deltaTime;
}
