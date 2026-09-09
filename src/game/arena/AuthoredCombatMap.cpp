#include "game/arena/AuthoredCombatMap.h"

namespace game::arena {
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
