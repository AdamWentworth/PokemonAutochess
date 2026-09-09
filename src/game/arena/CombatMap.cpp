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
    // A diagonal must satisfy both directed cardinal routes around its corner.
    return !blocked[index(flankX)] && !blocked[index(flankZ)] &&
           cardinal(from, flankX) && cardinal(flankX, to) &&
           cardinal(from, flankZ) && cardinal(flankZ, to);
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
        if (map.canEngageMelee(mover, target)) return first(current);
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

} // namespace game::arena
