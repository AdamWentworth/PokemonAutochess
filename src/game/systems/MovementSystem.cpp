// MovementSystem.cpp
#include "MovementSystem.h"

#include "engine/core/EngineServices.h"
#include "engine/core/ecs/World.h"
#include "game/PhaseState.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr float kCostDiag = 1.414f;
constexpr float kCostStraight = 1.0f;
constexpr float kInfCost = std::numeric_limits<float>::max();

constexpr int kDirs[8][2] = {
    {-1, 0}, {1, 0}, {0, -1}, {0, 1},
    {-1, -1}, {1, 1}, {-1, 1}, {1, -1},
};

bool isCombatActive(const PokemonInstance& unit) {
    return unit.alive && !unit.captureInProgress;
}

bool setFacingToTarget(PokemonInstance& unit, const glm::vec3& targetPos) {
    const glm::vec3 delta = targetPos - unit.position;
    const float lenSq = glm::dot(delta, delta);
    if (lenSq <= 1e-8f) return false;

    const glm::vec3 lookDir = delta / std::sqrt(lenSq);
    constexpr float kRadToDeg = 57.29577951308232f;
    unit.rotation.y = std::atan2(lookDir.x, lookDir.z) * kRadToDeg;
    return true;
}

int cellIndex(const GameConfigData& cfg, int col, int row) {
    return row * cfg.cols + col;
}

bool inside(const GameConfigData& cfg, int col, int row) {
    return col >= 0 && col < cfg.cols && row >= 0 && row < cfg.rows;
}

float heuristic(int col, int row, int targetCol, int targetRow) {
    const int dx = std::abs(col - targetCol);
    const int dy = std::abs(row - targetRow);
    const int diag = std::min(dx, dy);
    return kCostDiag * static_cast<float>(diag) +
           kCostStraight * static_cast<float>(std::max(dx, dy) - diag);
}

struct PlannerUnit {
    PokemonInstance* unit = nullptr;
    int col = 0;
    int row = 0;
    float speed = 0.0f;
    int plannedCol = -1;
    int plannedRow = -1;
    int enemyCol = -1;
    int enemyRow = -1;
    bool adjacentToEnemy = false;
    float dist = kInfCost;
};

bool betterPriority(const PlannerUnit& a, const PlannerUnit& b) {
    if (a.dist != b.dist) return a.dist < b.dist;
    if (a.speed != b.speed) return a.speed > b.speed;
    return a.unit->id < b.unit->id;
}

std::pair<int, int> aStarFirstStep(const GameConfigData& cfg,
                                   int startCol,
                                   int startRow,
                                   int targetCol,
                                   int targetRow,
                                   const std::vector<std::uint8_t>& blocked) {
    const int totalCells = cfg.cols * cfg.rows;
    std::vector<int> openList;
    std::vector<std::uint8_t> openSet(totalCells, 0u);
    std::vector<float> g(totalCells, kInfCost);
    std::vector<int> parent(totalCells, -1);
    openList.reserve(totalCells);

    const int startCell = cellIndex(cfg, startCol, startRow);
    openList.push_back(startCell);
    openSet[startCell] = 1u;
    g[startCell] = 0.0f;

    while (!openList.empty()) {
        std::size_t bestPos = 0;
        float bestF = g[openList[0]] + heuristic(
            openList[0] % cfg.cols,
            openList[0] / cfg.cols,
            targetCol,
            targetRow);
        for (std::size_t i = 1; i < openList.size(); ++i) {
            const int idx = openList[i];
            const float fi = g[idx] + heuristic(idx % cfg.cols, idx / cfg.cols, targetCol, targetRow);
            if (fi < bestF) {
                bestPos = i;
                bestF = fi;
            }
        }

        const int curCell = openList[bestPos];
        const int curCol = curCell % cfg.cols;
        const int curRow = curCell / cfg.cols;
        openSet[curCell] = 0u;
        if (bestPos + 1 != openList.size()) {
            openList[bestPos] = openList.back();
        }
        openList.pop_back();

        if (std::max(std::abs(curCol - targetCol), std::abs(curRow - targetRow)) == 1) {
            int stepCell = curCell;
            int parentCell = parent[curCell];
            while (parentCell != -1) {
                const int grandParent = parent[parentCell];
                if (grandParent == -1) {
                    return {stepCell % cfg.cols, stepCell / cfg.cols};
                }
                stepCell = parentCell;
                parentCell = grandParent;
            }
            return {-1, -1};
        }

        const float curG = g[curCell];
        for (const auto& dir : kDirs) {
            const int nextCol = curCol + dir[0];
            const int nextRow = curRow + dir[1];
            if (!inside(cfg, nextCol, nextRow)) continue;

            const int nextCell = cellIndex(cfg, nextCol, nextRow);
            if (blocked[nextCell] != 0u) continue;

            const bool diag = (dir[0] != 0 && dir[1] != 0);
            const float nextG = curG + (diag ? kCostDiag : kCostStraight);
            if (nextG >= g[nextCell]) continue;

            g[nextCell] = nextG;
            parent[nextCell] = curCell;
            if (openSet[nextCell] == 0u) {
                openList.push_back(nextCell);
                openSet[nextCell] = 1u;
            }
        }
    }

    return {-1, -1};
}

}  // namespace

MovementSystem::MovementSystem(GameWorld* world,
                               GameServices& svc,
                               engine::ecs::Entity combatEntity_)
    : gameWorld(world), services(svc), combatEntity(combatEntity_) {}

MovementSystem::~MovementSystem() = default;

void MovementSystem::update(engine::ecs::World& ecsWorld, float deltaTime) {
    using Clock = std::chrono::high_resolution_clock;

    if (!ecsWorld.alive(combatEntity)) return;
    auto* combat = ecsWorld.get<game::CombatActive>(combatEntity);
    if (!combat || !combat->active) return;
    if (!gameWorld || gameWorld->isBoardResizePauseActive()) return;

    EngineFixedPerfBreakdown* fixedBreakdown =
        services.engineServices ? &services.engineServices->frameFixedBreakdown : nullptr;

    const auto& cfg = gameWorld->getConfig();
    auto& boardUnits = gameWorld->getPokemons();
    const int totalCells = cfg.cols * cfg.rows;

    const auto planStart = Clock::now();

    struct CachedUnit {
        int col = 0;
        int row = 0;
        PokemonSide side = PokemonSide::Player;
        float speed = 0.0f;
        bool active = false;
        bool blocksTile = false;
    };

    std::vector<CachedUnit> cached;
    cached.reserve(boardUnits.size());

    std::vector<int> occupantByCell(totalCells, -1);
    std::vector<std::uint8_t> blocked(totalCells, 0u);

    for (const auto& unit : boardUnits) {
        CachedUnit item;
        const glm::ivec2 cell = gameWorld->worldToGrid(unit.position);
        item.col = cell.x;
        item.row = cell.y;
        item.side = unit.side;
        item.speed = unit.movementSpeed;
        item.active = isCombatActive(unit);
        item.blocksTile = item.active || unit.captureInProgress || (unit.fainting && cfg.faintBlockTiles);
        cached.push_back(item);

        if (!item.blocksTile) continue;
        if (!inside(cfg, item.col, item.row)) continue;
        const int idx = cellIndex(cfg, item.col, item.row);
        blocked[idx] = 1u;
        occupantByCell[idx] = unit.id;
    }

    std::vector<PlannerUnit> units;
    units.reserve(boardUnits.size());
    for (std::size_t i = 0; i < boardUnits.size(); ++i) {
        if (!cached[i].active) continue;

        PlannerUnit entry;
        entry.unit = &boardUnits[i];
        entry.col = cached[i].col;
        entry.row = cached[i].row;
        entry.speed = cached[i].speed;
        entry.plannedCol = boardUnits[i].committedDest.x;
        entry.plannedRow = boardUnits[i].committedDest.y;

        int bestDistance = std::numeric_limits<int>::max();
        for (std::size_t j = 0; j < boardUnits.size(); ++j) {
            if (i == j) continue;
            if (!cached[j].active || cached[j].side == cached[i].side) continue;

            const int dx = std::abs(entry.col - cached[j].col);
            const int dy = std::abs(entry.row - cached[j].row);
            const int dist = std::max(dx, dy);
            if (dist < bestDistance) {
                bestDistance = dist;
                entry.enemyCol = cached[j].col;
                entry.enemyRow = cached[j].row;
            }
        }

        entry.adjacentToEnemy = (bestDistance == 1);
        if (entry.enemyCol != -1) {
            const int dx = entry.col - entry.enemyCol;
            const int dy = entry.row - entry.enemyRow;
            entry.dist = static_cast<float>(dx * dx + dy * dy);
        }

        units.push_back(entry);
    }

    std::sort(units.begin(), units.end(), [](const PlannerUnit& a, const PlannerUnit& b) {
        return betterPriority(a, b);
    });

    std::vector<int> desiredCols(units.size(), -1);
    std::vector<int> desiredRows(units.size(), -1);
    std::vector<int> desiredCells(units.size(), -1);
    std::vector<int> claimedByCell(totalCells, -1);

    for (std::size_t i = 0; i < units.size(); ++i) {
        const PlannerUnit& unit = units[i];

        int wantCol = unit.col;
        int wantRow = unit.row;

        if (unit.unit->isMoving && unit.plannedCol >= 0 && unit.plannedRow >= 0) {
            wantCol = unit.plannedCol;
            wantRow = unit.plannedRow;
        } else if (!unit.adjacentToEnemy && unit.enemyCol != -1) {
            const auto next = aStarFirstStep(cfg, unit.col, unit.row, unit.enemyCol, unit.enemyRow, blocked);
            if (next.first >= 0 && next.second >= 0) {
                wantCol = next.first;
                wantRow = next.second;
            }
        }

        int wantCell = inside(cfg, wantCol, wantRow) ? cellIndex(cfg, wantCol, wantRow) : -1;
        if (wantCell < 0 || claimedByCell[wantCell] != -1) {
            wantCol = unit.col;
            wantRow = unit.row;
            wantCell = cellIndex(cfg, wantCol, wantRow);
        }

        desiredCols[i] = wantCol;
        desiredRows[i] = wantRow;
        desiredCells[i] = wantCell;
        claimedByCell[wantCell] = static_cast<int>(i);
        blocked[wantCell] = 1u;
    }

    std::vector<int> cellWinner(totalCells, -1);
    std::vector<std::uint8_t> winners(units.size(), 0u);
    for (std::size_t i = 0; i < units.size(); ++i) {
        const int wantCell = desiredCells[i];
        if (wantCell < 0) continue;

        const int incumbent = cellWinner[wantCell];
        if (incumbent < 0 || betterPriority(units[i], units[incumbent])) {
            cellWinner[wantCell] = static_cast<int>(i);
        }
    }
    for (int winner : cellWinner) {
        if (winner >= 0) winners[winner] = 1u;
    }

    for (std::size_t i = 0; i < units.size(); ++i) {
        if (winners[i] == 0u) continue;

        const int wantCell = desiredCells[i];
        if (wantCell < 0) continue;

        const int otherId = occupantByCell[wantCell];
        if (otherId < 0 || otherId == units[i].unit->id) continue;

        int otherIndex = -1;
        for (std::size_t j = 0; j < units.size(); ++j) {
            if (units[j].unit->id == otherId) {
                otherIndex = static_cast<int>(j);
                break;
            }
        }
        if (otherIndex < 0 || winners[otherIndex] == 0u) continue;

        if (desiredCols[otherIndex] == units[i].col &&
            desiredRows[otherIndex] == units[i].row) {
            winners[i] = 0u;
            winners[otherIndex] = 0u;
        }
    }

    for (std::size_t i = 0; i < units.size(); ++i) {
        PokemonInstance& unit = *units[i].unit;
        if (winners[i] == 0u || unit.isMoving) continue;

        const int wantCol = desiredCols[i];
        const int wantRow = desiredRows[i];
        if (wantCol == unit.committedDest.x && wantRow == unit.committedDest.y) continue;
        if (wantCol == units[i].col && wantRow == units[i].row) continue;

        unit.committedDest = {wantCol, wantRow};
        unit.moveFrom = unit.position;
        unit.moveTo = gameWorld->gridToWorld(wantCol, wantRow);
        unit.moveT = 0.0f;
        unit.isMoving = true;
    }

    for (const PlannerUnit& unit : units) {
        if (unit.enemyCol != -1 && unit.enemyRow != -1) {
            setFacingToTarget(*unit.unit, gameWorld->gridToWorld(unit.enemyCol, unit.enemyRow));
        }
    }

    if (fixedBreakdown) {
        fixedBreakdown->movementPlanMs += static_cast<float>(
            std::chrono::duration<double, std::milli>(Clock::now() - planStart).count());
    }

    auto& worldUnits = gameWorld->getPokemons();
    const auto advanceStart = Clock::now();

    const float cellSize = std::max(cfg.cellSize, 1e-4f);
    for (auto& unit : worldUnits) {
        if (!unit.alive) continue;
        if (!unit.isMoving) continue;

        const glm::vec3 toVec = unit.moveTo - unit.position;
        const float dist = glm::length(toVec);
        if (dist <= 1e-4f) {
            unit.position = unit.moveTo;
            unit.isMoving = false;
            unit.moveT = 1.0f;
            unit.committedDest = {-1, -1};
            continue;
        }

        const glm::vec3 dir = toVec / dist;
        const float step = unit.movementSpeed * cellSize * deltaTime;
        if (step >= dist) {
            unit.position = unit.moveTo;
            unit.isMoving = false;
            unit.moveT = 1.0f;
            unit.committedDest = {-1, -1};
        } else {
            unit.position += dir * step;
            unit.moveT = std::min(1.0f, unit.moveT + (step / cellSize));
        }
    }

    if (fixedBreakdown) {
        fixedBreakdown->movementAdvanceMs += static_cast<float>(
            std::chrono::duration<double, std::milli>(Clock::now() - advanceStart).count());
    }
}
