#include "game/arena/CombatMap.h"

#include <limits>
#include <vector>

namespace game::arena {
namespace {
constexpr float kDiagonal = 1.414f;
constexpr int kDirections[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, 1}, {-1, 1}, {1, -1}};
float heuristic(Cell from, Cell to) {
    const auto dx = std::abs(static_cast<std::int64_t>(from.x) - to.x), dz = std::abs(static_cast<std::int64_t>(from.z) - to.z);
    const auto diagonal = std::min(dx, dz);
    return kDiagonal * diagonal + static_cast<float>(std::max(dx, dz) - diagonal);
}
bool physicalMelee(CombatMapView map, const Actor &mover, const Actor &target) {
    return !mover.traversingLedge &&
           std::max(std::abs(mover.cell.x - target.cell.x), std::abs(mover.cell.z - target.cell.z)) == 1 &&
           (!map.rules || map.rules->canEngageMelee(mover, target));
}
bool validGrid(CombatMapView map, std::size_t size) {
    return map.cols > 0 && map.rows > 0 && map.cols <= 256 && map.rows <= 256 &&
           size == static_cast<std::size_t>(map.cols) * map.rows;
}
} // namespace

bool CombatMapView::canStep(Cell from, Cell to, TraversalCapabilities capabilities,
                            std::span<const std::uint8_t> blocked) const {
    if (!validGrid(*this, blocked.size()) || !contains(from) || !contains(to) || from == to ||
        std::abs(to.x - from.x) > 1 || std::abs(to.z - from.z) > 1 || blocked[index(to)]) return false;
    const auto cardinal = [&](Cell a, Cell b) {
        return !rules || rules->canTraverseCardinal(a, b, capabilities);
    };
    if (from.x == to.x || from.z == to.z) return cardinal(from, to);
    const Cell flankX{to.x, from.z}, flankZ{from.x, to.z};
    const auto walk = [&](Cell a, Cell b) {
        return !rules || rules->cardinalStep(a, b, capabilities) == StepKind::Walk;
    };
    // A diagonal must satisfy both directed cardinal routes around its corner.
    return !blocked[index(flankX)] && !blocked[index(flankZ)] &&
           walk(from, flankX) && walk(flankX, to) &&
           walk(from, flankZ) && walk(flankZ, to);
}

bool canReachMelee(CombatMapView map, Actor mover, const Actor &target) {
    if (!map.contains(mover.cell) || !map.contains(target.cell) || !map.canPerceive(mover, target)) return false;
    if (!map.rules) return true;
    if (map.cols <= 0 || map.rows <= 0 || map.cols > 256 || map.rows > 256) return false;
    std::vector<std::uint8_t> blocked(map.cols * map.rows), visited(map.cols * map.rows);
    blocked[map.index(target.cell)] = 1;
    std::vector<Cell> queue{mover.cell};
    visited[map.index(mover.cell)] = 1;
    for (std::size_t i = 0; i < queue.size(); ++i) {
        mover.cell = queue[i];
        if (physicalMelee(map, mover, target)) return true;
        for (const auto &direction : kDirections) {
            const Cell next{mover.cell.x + direction[0], mover.cell.z + direction[1]};
            if (!map.contains(next) || visited[map.index(next)] || !map.canStep(mover.cell, next, mover.traversal, blocked)) continue;
            visited[map.index(next)] = 1;
            queue.push_back(next);
        }
    }
    return false;
}

void reserveStep(CombatMapView map, Cell from, Cell to, std::span<std::uint8_t> blocked) {
    if (!validGrid(map, blocked.size())) return;
    for (int z = std::max(0, std::min(from.z, to.z)); z <= std::min(map.rows - 1, std::max(from.z, to.z)); ++z)
        for (int x = std::max(0, std::min(from.x, to.x)); x <= std::min(map.cols - 1, std::max(from.x, to.x)); ++x)
            blocked[map.index({x, z})] = 1u;
}

Cell firstStepTowards(CombatMapView map, Actor mover, const Actor &target,
                      std::span<const std::uint8_t> blocked) {
    if (!validGrid(map, blocked.size()) || !map.contains(mover.cell) || !map.canPerceive(mover, target)) return {};
    const int total = map.cols * map.rows, start = map.index(mover.cell);
    std::vector<int> open{start}, parent(total, -1);
    std::vector<std::uint8_t> queued(total, 0);
    std::vector<float> cost(total, std::numeric_limits<float>::max());
    open.reserve(total);
    queued[start] = 1;
    cost[start] = 0;
    int closest = start;
    float closestDistance = heuristic(mover.cell, target.cell);
    const auto cell = [&](int index) { return Cell{index % map.cols, index / map.cols}; };
    const auto first = [&](int destination) {
        if (destination == start) return Cell{};
        int step = destination;
        while (parent[step] != -1 && parent[step] != start)
            step = parent[step];
        return cell(step);
    };
    while (!open.empty()) {
        std::size_t best = 0;
        float bestF = cost[open[0]] + heuristic(cell(open[0]), target.cell);
        for (std::size_t i = 1; i < open.size(); ++i) {
            const float f = cost[open[i]] + heuristic(cell(open[i]), target.cell);
            if (f < bestF) {
                best = i;
                bestF = f;
            }
        }
        const int current = open[best];
        queued[current] = 0;
        open[best] = open.back();
        open.pop_back();
        mover.cell = cell(current);
        if (physicalMelee(map, mover, target)) return first(current);
        const float distance = heuristic(mover.cell, target.cell);
        if (distance < closestDistance || (current != start && distance == closestDistance && cost[current] < cost[closest])) {
            closest = current;
            closestDistance = distance;
        }
        for (const auto &direction : kDirections) {
            const Cell next{mover.cell.x + direction[0], mover.cell.z + direction[1]};
            if (!map.canStep(mover.cell, next, mover.traversal, blocked)) continue;
            const int nextIndex = map.index(next);
            const float nextCost = cost[current] + (direction[0] && direction[1] ? kDiagonal : 1.0f);
            if (nextCost >= cost[nextIndex]) continue;
            cost[nextIndex] = nextCost;
            parent[nextIndex] = current;
            if (!queued[nextIndex]) {
                open.push_back(nextIndex);
                queued[nextIndex] = 1;
            }
        }
    }
    return first(closest);
}

Cell firstStepToCell(CombatMapView map, Actor mover, Cell destination,
                     std::span<const std::uint8_t> blocked) {
    if (!validGrid(map, blocked.size()) || !map.contains(mover.cell) ||
        !map.contains(destination) || destination == mover.cell || blocked[map.index(destination)]) return {};
    std::vector<int> parents(blocked.size(), -1);
    std::vector<Cell> queue{mover.cell};
    const int start = map.index(mover.cell);
    parents[start] = start;
    for (std::size_t i = 0; i < queue.size(); ++i) {
        const Cell current = queue[i];
        for (const auto &direction : kDirections) {
            const Cell next{current.x + direction[0], current.z + direction[1]};
            if (!map.contains(next) || parents[map.index(next)] >= 0 ||
                !map.canStep(current, next, mover.traversal, blocked)) continue;
            int index = map.index(next);
            parents[index] = map.index(current);
            if (next == destination) {
                while (parents[index] != start)
                    index = parents[index];
                return {index % map.cols, index / map.cols};
            }
            queue.push_back(next);
        }
    }
    return {};
}

Cell firstPatrolStep(CombatMapView map, Actor mover, PatrolState &state,
                     std::span<const std::uint8_t> blocked, bool enemyEndIsNorth) {
    if (!validGrid(map, blocked.size()) || !map.contains(mover.cell)) return {};
    const int count = map.cols * map.rows;
    if (state.startColumn < 0 || state.startColumn >= map.cols) {
        state.startColumn = mover.cell.x;
        state.cursor = enemyEndIsNorth ? map.rows - 1 - mover.cell.z : mover.cell.z;
    }
    state.cursor = std::clamp(state.cursor, 0, count - 1);
    for (int attempt = 0; attempt < count; ++attempt) {
        const int lane = state.cursor / map.rows, step = state.cursor % map.rows;
        const bool north = enemyEndIsNorth != ((lane % 2) != 0);
        const Cell destination{(state.startColumn + lane) % map.cols, north ? map.rows - 1 - step : step};
        const Cell next = firstStepToCell(map, mover, destination, blocked);
        if (next != Cell{}) return next;
        // Reached, occupied or unreachable: continue the sweep instead of
        // getting stuck on a wall or on a hidden unit's occupied square.
        state.cursor = (state.cursor + 1) % count;
    }
    return {};
}

} // namespace game::arena
