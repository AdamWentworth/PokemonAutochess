// src/game/systems/UnitInteractionSystem.cpp
#include "UnitInteractionSystem.h"

#include "game/GameConfig.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/common.hpp>
#include <algorithm>
#include <cmath>
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
    return pos.z >= (cellSize * 0.5f) && pos.z <= (cellSize * 4.0f);
}

bool UnitInteractionSystem::isSameCell(const glm::vec3& a, const glm::vec3& b, float eps) const {
    return (std::abs(a.x - b.x) <= eps) && (std::abs(a.z - b.z) <= eps);
}

bool UnitInteractionSystem::isBoardCellOccupied(const glm::vec3& pos, int ignoreIndex) const {
    if (!gameWorld) return false;
    const auto& board = gameWorld->getPokemons();
    for (int i = 0; i < static_cast<int>(board.size()); ++i) {
        if (i == ignoreIndex) continue;
        const bool blocks = board[i].alive ||
                            board[i].captureInProgress ||
                            (board[i].fainting && gameWorld->getConfig().faintBlockTiles);
        if (!blocks) continue;
        if (isSameCell(board[i].position, pos)) return true;
    }
    return false;
}

bool UnitInteractionSystem::isBenchSlotOccupied(const glm::vec3& pos, int ignoreIndex) const {
    if (!gameWorld) return false;
    const auto& bench = gameWorld->getBenchPokemons();
    for (int i = 0; i < static_cast<int>(bench.size()); ++i) {
        if (i == ignoreIndex) continue;
        if (!bench[i].alive) continue;
        if (isSameCell(bench[i].position, pos)) return true;
    }
    return false;
}

void UnitInteractionSystem::onMouseButtonDown(int x, int y) {
    glm::vec3 worldPos = screenToWorld(x, y);
    worldPos.y = 0.0f;

    // Item use handling (combat only)
    if (gameWorld) {
        const std::string selected = gameWorld->getSelectedItem();
        if (!selected.empty()) {
            const bool inCombat = gameWorld->isBoardInteractionLocked();
            if (!inCombat) {
                gameWorld->clearSelectedItem();
                return;
            }

            // Find closest target on board
            PokemonInstance* closest = nullptr;
            float best = std::numeric_limits<float>::max();

            auto& board = gameWorld->getPokemons();
            for (auto& u : board) {
                if (u.captureInProgress) continue;

                const bool isPokeball = (selected == "pokeball");
                if (isPokeball) {
                    if (u.side != PokemonSide::Enemy) continue;
                    if (!u.alive && !u.fainting) continue;
                } else {
                    if (u.side != PokemonSide::Player) continue;
                    if (!u.alive) continue;
                }

                float d = glm::distance(worldPos, u.position);
                if (d < best) {
                    best = d;
                    closest = &u;
                }
            }

            if (closest && best <= pickRadius) {
                if (selected == "pokeball") {
                    if (gameWorld->getItemCount(selected) > 0) {
                        glm::vec3 throwOrigin = closest->position + glm::vec3(0.0f, 0.0f, -gameWorld->getConfig().cellSize * 3.0f);
                        if (camera) {
                            const glm::vec3 dir = glm::normalize(camera->getDirection());
                            glm::vec3 right = glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f));
                            if (glm::length(right) < 0.001f) {
                                right = glm::vec3(1.0f, 0.0f, 0.0f);
                            } else {
                                right = glm::normalize(right);
                            }

                            const float forward = std::max(0.5f, gameWorld->getConfig().cellSize * 2.5f);
                            const float side = std::max(0.2f, gameWorld->getConfig().cellSize * 0.7f);
                            throwOrigin = camera->getPosition()
                                + dir * forward
                                + right * side;
                        }
                        if (gameWorld->startCaptureAttempt(closest->id, 1.0f, &throwOrigin)) {
                            gameWorld->consumeItem(selected, 1);
                        }
                    }
                } else {
                    gameWorld->tryUseHealingItem(selected, closest->id);
                }
            }

            gameWorld->clearSelectedItem();
            return;
        }
    }

    if (!draggingUnit) {
        // PICKUP
        if (!gameWorld) return;
        const bool boardLocked = gameWorld->isBoardInteractionLocked();
        float best = std::numeric_limits<float>::max();
        int idx = -1;
        draggingFromBench = false;

        if (!boardLocked) {
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
            dragStartPos = draggingFromBench
                ? gameWorld->getBenchPokemons()[draggedIndex].position
                : gameWorld->getPokemons()[draggedIndex].position;
        }
    } else {
        // DROP
        const bool boardLocked = gameWorld ? gameWorld->isBoardInteractionLocked() : false;
        glm::vec3 snap = worldPos;
        bool toBench = isInBenchZone(worldPos);
        bool toBoard = isInBoardZone(worldPos);

        if (toBench) {
            snap = benchSystem.getSnappedBenchPosition(worldPos);
        } else if (toBoard) {
            float boardOriginX = -((8 * cellSize) / 2.0f) + cellSize * 0.5f;
            float boardOriginZ = cellSize * 0.5f;

            int col = static_cast<int>(std::round((worldPos.x - boardOriginX) / cellSize));
            int row = static_cast<int>(std::round((worldPos.z - boardOriginZ) / cellSize));

            col = glm::clamp(col, 0, 7);
            row = glm::clamp(row, 0, 3);

            snap.x = boardOriginX + col * cellSize;
            snap.z = boardOriginZ + row * cellSize;
            snap.y = 0.0f;
        }

        if (toBench && !draggingFromBench) {
            if (boardLocked) {
                gameWorld->getPokemons()[draggedIndex].position = dragStartPos;
            } else {
                if (isBenchSlotOccupied(snap, -1)) {
                    gameWorld->getPokemons()[draggedIndex].position = dragStartPos;
                } else {
                    auto& board = gameWorld->getPokemons();
                    auto unit = board[draggedIndex];
                    board.erase(board.begin() + draggedIndex);
                    unit.position = snap;
                    gameWorld->getBenchPokemons().push_back(unit);
                    std::cout << "[UnitInteraction] Moved to bench\n";
                }
            }
        } else if (toBoard && draggingFromBench) {
            if (boardLocked) {
                gameWorld->getBenchPokemons()[draggedIndex].position = dragStartPos;
            } else {
                if (isBoardCellOccupied(snap, -1)) {
                    gameWorld->getBenchPokemons()[draggedIndex].position = dragStartPos;
                } else {
                    auto& bench = gameWorld->getBenchPokemons();
                    auto unit = bench[draggedIndex];
                    bench.erase(bench.begin() + draggedIndex);
                    unit.position = snap;
                    gameWorld->getPokemons().push_back(unit);
                    std::cout << "[UnitInteraction] Moved to board\n";
                }
            }
        } else if (!toBench && !toBoard) {
            if (draggingFromBench) {
                gameWorld->getBenchPokemons()[draggedIndex].position = dragStartPos;
            } else {
                gameWorld->getPokemons()[draggedIndex].position = dragStartPos;
            }
        }

        // reset drag
        draggingUnit = false;
        draggedIndex = -1;
        draggingFromBench = false;
    }
}

void UnitInteractionSystem::onMouseMotion(int x, int y) {
    if (draggingUnit && draggedIndex >= 0) {
        if (!gameWorld) return;
        const bool boardLocked = gameWorld->isBoardInteractionLocked();
        glm::vec3 rawPos = screenToWorld(x, y);
        glm::vec3 snappedPos = rawPos;

        if (boardLocked && !draggingFromBench) {
            return;
        }

        if (isInBenchZone(rawPos)) {
            snappedPos = benchSystem.getSnappedBenchPosition(rawPos);
            int ignore = draggingFromBench ? draggedIndex : -1;
            if (isBenchSlotOccupied(snappedPos, ignore)) return;
        } else if (!boardLocked) {
            float boardOriginX = -((8 * cellSize) / 2.0f) + cellSize * 0.5f;
            float boardOriginZ = cellSize * 0.5f;

            int col = static_cast<int>(std::round((rawPos.x - boardOriginX) / cellSize));
            int row = static_cast<int>(std::round((rawPos.z - boardOriginZ) / cellSize));

            col = glm::clamp(col, 0, 7);
            row = glm::clamp(row, 0, 3);

            snappedPos.x = boardOriginX + col * cellSize;
            snappedPos.z = boardOriginZ + row * cellSize;
            snappedPos.y = 0.0f;

            int ignore = draggingFromBench ? -1 : draggedIndex;
            if (isBoardCellOccupied(snappedPos, ignore)) return;
        } else {
            return;
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
