#include "game/arena/AuthoredCombatMap.h"

namespace game::arena {
namespace {
using Polygon = std::array<std::array<float, 2>, 4>;
// Convex polygon SAT. Dense clumps can share an edge, but a tiny contact at
// their outer corners must not merge otherwise separate authored patches.
bool connected(const Polygon &a, const Polygon &b) {
    std::array<float, 2> touchingAxis{};
    bool touching = false;
    for (const auto *polygon : {&a, &b}) {
        for (int edge = 0; edge < 4; ++edge) {
            const auto &p = (*polygon)[edge], &q = (*polygon)[(edge + 1) % 4];
            float nx = q[1] - p[1], nz = p[0] - q[0];
            const float length = std::hypot(nx, nz);
            if (length < 0.001f) continue;
            nx /= length;
            nz /= length;
            const auto range = [&](const Polygon &shape) {
                std::array<float, 2> r{1.0e30f, -1.0e30f};
                for (const auto &v : shape) {
                    const float projection = v[0] * nx + v[1] * nz;
                    r[0] = std::min(r[0], projection);
                    r[1] = std::max(r[1], projection);
                }
                return r;
            };
            const auto ra = range(a), rb = range(b);
            const float overlap = std::min(ra[1], rb[1]) - std::max(ra[0], rb[0]);
            if (overlap < -0.01f) return false;
            constexpr float kMinimumConnectionCm = 10.0f;
            if (overlap <= kMinimumConnectionCm) {
                if (touching && std::abs(touchingAxis[0] * nz - touchingAxis[1] * nx) > 0.001f) return false;
                touching = true;
                touchingAxis = {nx, nz};
            }
        }
    }
    return true;
}
} // namespace

AuthoredCombatMap::AuthoredCombatMap(ArenaMapData data, Cell origin)
    : data_(std::move(data)), origin_(origin) {
    std::vector<Polygon> polygons;
    for (const auto &region : data_.cover)
        polygons.insert(polygons.end(), region.polygons.begin(), region.polygons.end());
    std::vector<bool> assigned(polygons.size());
    for (std::size_t seed = 0; seed < polygons.size(); ++seed) {
        if (assigned[seed]) continue;
        CoverRegion group;
        std::vector<std::size_t> queue{seed};
        assigned[seed] = true;
        for (std::size_t i = 0; i < queue.size(); ++i) {
            group.polygons.push_back(polygons[queue[i]]);
            for (std::size_t j = 0; j < polygons.size(); ++j) {
                if (!assigned[j] && connected(polygons[queue[i]], polygons[j])) {
                    assigned[j] = true;
                    queue.push_back(j);
                }
            }
        }
        coverGroups_.push_back(std::move(group));
    }
}

int AuthoredCombatMap::coverGroup(const Actor &actor) const {
    if (!actor.grounded || actor.traversingLedge) return -1;
    const auto source = sourceCell(actor.cell);
    const float x = (source.x + 0.5f + actor.offsetX) * 100.0f;
    const float z = (source.z + 0.5f + actor.offsetZ) * 100.0f;
    for (std::size_t i = 0; i < coverGroups_.size(); ++i)
        if (coverGroups_[i].contains(x, z)) return static_cast<int>(i);
    return -1;
}

bool AuthoredCombatMap::canPerceive(const Actor &observer, const Actor &target) const {
    if (observer.team == target.team || target.revealed) return true;
    const int targetGroup = coverGroup(target);
    return targetGroup < 0 || targetGroup == coverGroup(observer);
}

StepKind AuthoredCombatMap::cardinalStep(Cell from, Cell to, TraversalCapabilities capability) const {
    if (std::abs(to.x - from.x) + std::abs(to.z - from.z) != 1) return StepKind::Blocked;
    from = sourceCell(from);
    to = sourceCell(to);
    if (!data_.playableCells.contains({from.x, from.z}) || !data_.playableCells.contains({to.x, to.z})) return StepKind::Blocked;
    if (capability.ignoresTerrain) return StepKind::Walk;
    const auto delta = data_.edgeHeightDelta(from, to);
    constexpr float toleranceCm = 0.01f;
    if (std::abs(delta[0]) <= toleranceCm && std::abs(delta[1]) <= toleranceCm) return StepKind::Walk;
    // Only full, level, south-facing ledges are droppable. Ramp side walls,
    // uphill approaches and east/west/north-facing cliffs remain walls.
    if (to.z == from.z + 1 && delta[0] < -toleranceCm &&
        std::abs(delta[0] - delta[1]) <= toleranceCm) return StepKind::LedgeDrop;
    return StepKind::Blocked;
}

bool AuthoredCombatMap::canEngageMelee(const Actor &a, const Actor &b) const {
    const auto walk = [&](Cell from, Cell to) {
        // A flying attacker can approach either elevation. Ground melee cannot
        // reach through a cliff even when it could jump down it.
        return cardinalStep(from, to, a.traversal) == StepKind::Walk;
    };
    if (a.cell.x == b.cell.x || a.cell.z == b.cell.z) return walk(a.cell, b.cell);
    const Cell x{b.cell.x, a.cell.z}, z{a.cell.x, b.cell.z};
    return walk(a.cell, x) && walk(x, b.cell) && walk(a.cell, z) && walk(z, b.cell);
}
} // namespace game::arena
