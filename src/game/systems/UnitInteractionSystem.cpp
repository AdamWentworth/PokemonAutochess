// src/game/systems/UnitInteractionSystem.cpp
#include "UnitInteractionSystem.h"

#include "game/GameConfig.h"
#include "game/logging/LogBus.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/common.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

UnitInteractionSystem::UnitInteractionSystem(Camera3D* cam, GameWorld* world, unsigned int w, unsigned int h)
    : camera(cam),
      gameWorld(world),
      screenW(w),
      screenH(h),
      benchSystem(world ? world->getConfig().cellSize : 1.2f,
                  world ? world->getConfig().benchSlots : 8) {
    cellSize = world ? world->getConfig().cellSize : 1.2f;

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

glm::vec3 UnitInteractionSystem::snapBoardPosition(const glm::vec3& worldPos) const {
    float boardOriginX = -((8 * cellSize) / 2.0f) + cellSize * 0.5f;
    float boardOriginZ = cellSize * 0.5f;

    int col = static_cast<int>(std::round((worldPos.x - boardOriginX) / cellSize));
    int row = static_cast<int>(std::round((worldPos.z - boardOriginZ) / cellSize));

    col = glm::clamp(col, 0, 7);
    row = glm::clamp(row, 0, 3);

    glm::vec3 snap = worldPos;
    snap.x = boardOriginX + col * cellSize;
    snap.z = boardOriginZ + row * cellSize;
    snap.y = 0.0f;
    return snap;
}

bool UnitInteractionSystem::findNearestAvailableBoardCell(const glm::vec3& worldPos,
                                                          int ignoreIndex,
                                                          glm::vec3& outPos) const {
    bool found = false;
    float bestDist2 = std::numeric_limits<float>::max();
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 8; ++col) {
            glm::vec3 candidate;
            candidate.x = -((8 * cellSize) / 2.0f) + cellSize * 0.5f + static_cast<float>(col) * cellSize;
            candidate.z = cellSize * 0.5f + static_cast<float>(row) * cellSize;
            candidate.y = 0.0f;
            if (isBoardCellOccupied(candidate, ignoreIndex)) continue;
            const float dx = worldPos.x - candidate.x;
            const float dz = worldPos.z - candidate.z;
            const float d2 = dx * dx + dz * dz;
            if (d2 < bestDist2) {
                bestDist2 = d2;
                outPos = candidate;
                found = true;
            }
        }
    }
    return found;
}

bool UnitInteractionSystem::findNearestAvailableBenchSlot(const glm::vec3& worldPos,
                                                          int ignoreIndex,
                                                          glm::vec3& outPos) const {
    bool found = false;
    float bestDist2 = std::numeric_limits<float>::max();
    const int maxSlots = std::max(1, benchSystem.getMaxSlots());
    for (int slot = 0; slot < maxSlots; ++slot) {
        const glm::vec3 candidate = benchSystem.getSlotPosition(slot);
        if (isBenchSlotOccupied(candidate, ignoreIndex)) continue;
        const float dx = worldPos.x - candidate.x;
        const float dz = worldPos.z - candidate.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            outPos = candidate;
            found = true;
        }
    }
    return found;
}

bool UnitInteractionSystem::isInBenchZone(const glm::vec3& pos) const {
    return benchSystem.isInBenchZone(pos);
}

bool UnitInteractionSystem::isInBoardZone(const glm::vec3& pos) const {
    return pos.z >= (cellSize * 0.5f) && pos.z <= (cellSize * 4.0f);
}

bool UnitInteractionSystem::isInSellDropZoneScreen(int mouseX, int mouseY) const {
    const float w = static_cast<float>(std::max(1u, screenW));
    const float h = static_cast<float>(std::max(1u, screenH));

    const float zoneW = w * 0.70f;
    const float zoneH = glm::clamp(h * 0.28f, 120.0f, 300.0f);
    const float x0 = (w - zoneW) * 0.5f;
    const float y0 = h - zoneH;
    const float x1 = x0 + zoneW;
    const float y1 = h;

    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);
    return (mx >= x0 && mx <= x1 && my >= y0 && my <= y1);
}

void UnitInteractionSystem::setScreenSize(unsigned int width, unsigned int height) {
    screenW = (width > 0) ? width : 1;
    screenH = (height > 0) ? height : 1;
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
            gameWorld->setUnitDragActive(true);
            std::cout << "[UnitInteraction] Picked up index " << idx
                      << (draggingFromBench ? " from bench\n" : " from board\n");
            dragStartPos = draggingFromBench
                ? gameWorld->getBenchPokemons()[draggedIndex].position
                : gameWorld->getPokemons()[draggedIndex].position;
        }
    } else {
        // DROP
        const bool boardLocked = gameWorld ? gameWorld->isBoardInteractionLocked() : false;
        const bool canSell = gameWorld && !gameWorld->getClassicShopCards().empty();
        const bool toSell = canSell && isInSellDropZoneScreen(x, y);
        bool toBench = isInBenchZone(worldPos);
        bool toBoard = isInBoardZone(worldPos);
        bool completeDrop = false;

        if (toSell) {
            auto sellUnit = [&](const PokemonInstance& unit) {
                const int sellValue = gameWorld->getSellValueForSpecies(unit.name);
                gameWorld->addMoney(sellValue);
                if (auto* logger = gameWorld->getLogger()) {
                    logger->economyInfo("Earned +" + std::to_string(sellValue) + "g. Gold: " +
                                        std::to_string(gameWorld->getMoney()) + "g.");
                }
                std::cout << "[UnitInteraction] Sold " << unit.name << " for " << sellValue << "g\n";
            };

            if (draggingFromBench) {
                auto& bench = gameWorld->getBenchPokemons();
                if (draggedIndex >= 0 && draggedIndex < static_cast<int>(bench.size())) {
                    PokemonInstance unit = bench[draggedIndex];
                    bench.erase(bench.begin() + draggedIndex);
                    sellUnit(unit);
                    completeDrop = true;
                }
            } else if (!boardLocked) {
                auto& board = gameWorld->getPokemons();
                if (draggedIndex >= 0 && draggedIndex < static_cast<int>(board.size())) {
                    PokemonInstance unit = board[draggedIndex];
                    board.erase(board.begin() + draggedIndex);
                    sellUnit(unit);
                    completeDrop = true;
                }
            }
        } else if (draggingFromBench) {
            if (toBoard && !boardLocked) {
                glm::vec3 snap{};
                if (findNearestAvailableBoardCell(worldPos, -1, snap)) {
                    auto& bench = gameWorld->getBenchPokemons();
                    auto unit = bench[draggedIndex];
                    bench.erase(bench.begin() + draggedIndex);
                    unit.position = snap;
                    gameWorld->getPokemons().push_back(unit);
                    std::cout << "[UnitInteraction] Moved to board\n";
                    completeDrop = true;
                }
            }
            if (!completeDrop && toBench) {
                glm::vec3 snap{};
                if (findNearestAvailableBenchSlot(worldPos, draggedIndex, snap)) {
                    gameWorld->getBenchPokemons()[draggedIndex].position = snap;
                    completeDrop = true;
                }
            }
        } else {
            if (toBench && !boardLocked) {
                glm::vec3 snap{};
                if (findNearestAvailableBenchSlot(worldPos, -1, snap)) {
                    auto& board = gameWorld->getPokemons();
                    auto unit = board[draggedIndex];
                    board.erase(board.begin() + draggedIndex);
                    unit.position = snap;
                    gameWorld->getBenchPokemons().push_back(unit);
                    std::cout << "[UnitInteraction] Moved to bench\n";
                    completeDrop = true;
                }
            }
            if (!completeDrop && toBoard && !boardLocked) {
                glm::vec3 snap{};
                if (findNearestAvailableBoardCell(worldPos, draggedIndex, snap)) {
                    gameWorld->getPokemons()[draggedIndex].position = snap;
                    completeDrop = true;
                }
            }
        }

        if (completeDrop) {
            draggingUnit = false;
            draggedIndex = -1;
            draggingFromBench = false;
            gameWorld->setUnitDragActive(false);
            gameWorld->blockUiClicks(1);
        }
    }
}

void UnitInteractionSystem::onMouseMotion(int x, int y) {
    if (draggingUnit && draggedIndex >= 0) {
        if (!gameWorld) return;
        const bool boardLocked = gameWorld->isBoardInteractionLocked();
        glm::vec3 rawPos = screenToWorld(x, y);
        rawPos.y = 0.0f;

        if (boardLocked && !draggingFromBench) {
            return;
        }

        if (draggingFromBench) {
            gameWorld->getBenchPokemons()[draggedIndex].position = rawPos;
        } else {
            gameWorld->getPokemons()[draggedIndex].position = rawPos;
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
