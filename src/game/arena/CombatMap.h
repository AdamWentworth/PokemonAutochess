#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace game::arena {

struct Cell {
    int x = -1;
    int z = -1;
    bool operator==(const Cell &) const = default;
};

// Gameplay capabilities are independent of airborne animation/presentation.
struct TraversalCapabilities {
    bool ignoresTerrain = false;
};

enum class StepKind { Blocked,
                      Walk,
                      LedgeDrop };

struct Actor {
    int id = -1;
    int team = 0;
    Cell cell;
    TraversalCapabilities traversal;
    bool traversingLedge = false;
    bool grounded = true;
    bool revealed = false;
    // Continuous position relative to the cell centre, in tile units.
    float offsetX = 0.0f, offsetZ = 0.0f;
};

// Null rules retain today's flat-board movement and full visibility. Future
// terrain/cover rules belong here and are shared by planning and targeting.
class CombatMapRules {
  public:
    virtual ~CombatMapRules() = default;
    virtual bool canTraverseCardinal(Cell, Cell, TraversalCapabilities) const = 0;
    virtual StepKind cardinalStep(Cell from, Cell to, TraversalCapabilities capability) const {
        return canTraverseCardinal(from, to, capability) ? StepKind::Walk : StepKind::Blocked;
    }
    virtual bool canPerceive(const Actor &, const Actor &) const = 0;
    virtual bool canEngageMelee(const Actor &, const Actor &) const = 0;
};

struct CombatMapView {
    int cols = 0;
    int rows = 0;
    const CombatMapRules *rules = nullptr;

    bool contains(Cell cell) const noexcept {
        return cell.x >= 0 && cell.z >= 0 && cell.x < cols && cell.z < rows;
    }
    int index(Cell cell) const noexcept { return cell.z * cols + cell.x; }
    bool canPerceive(const Actor &observer, const Actor &target) const {
        return !rules || rules->canPerceive(observer, target);
    }
    bool canEngageMelee(const Actor &attacker, const Actor &target) const {
        if (attacker.traversingLedge) return false;
        return std::max(std::abs(static_cast<std::int64_t>(attacker.cell.x) - target.cell.x), std::abs(static_cast<std::int64_t>(attacker.cell.z) - target.cell.z)) == 1 &&
               canPerceive(attacker, target) && (!rules || rules->canEngageMelee(attacker, target));
    }
    bool canStep(Cell from, Cell to, TraversalCapabilities capabilities,
                 std::span<const std::uint8_t> blocked) const;
    StepKind stepKind(Cell from, Cell to, TraversalCapabilities capability) const {
        if (!rules || (from.x != to.x && from.z != to.z)) return StepKind::Walk;
        return rules->cardinalStep(from, to, capability);
    }
};

// Both diagonal corridors stay reserved until arrival. Terrain permissions do
// not allow flying actors to overlap occupied cells.
void reserveStep(CombatMapView map, Cell from, Cell to, std::span<std::uint8_t> blocked);
// Visibility uses the observed target. A visible mover's reserved destination
// can be used as the pursuit goal, including waiting for it to arrive in melee.
Cell firstStepTowards(CombatMapView map, Actor mover, const Actor &observedTarget,
                      std::span<const std::uint8_t> blocked, Cell targetDestination = {});
// Terrain reachability ignores temporary unit queues, but never treats the
// target's occupied cell as a route through a narrow passage.
bool canReachMelee(CombatMapView map, Actor mover, const Actor &target);

inline constexpr float kAttackRevealSeconds = 1.25f;

struct PatrolState {
    int cursor = 0;
    int startColumn = -1;
};
// Exact destination navigation; never substitutes an occupied/unreachable goal.
Cell firstStepToCell(CombatMapView map, Actor mover, Cell destination,
                     std::span<const std::uint8_t> blocked);
// Serpentine search starts toward the enemy end, then returns in the next lane.
// Visits reachable cells without taking any enemy positions as input.
Cell firstPatrolStep(CombatMapView map, Actor mover, PatrolState &state,
                     std::span<const std::uint8_t> blocked, bool enemyEndIsNorth);

} // namespace game::arena
