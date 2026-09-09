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

struct Actor {
    int id = -1;
    int team = 0;
    Cell cell;
    TraversalCapabilities traversal;
};

// Null rules retain today's flat-board movement and full visibility. Future
// terrain/cover rules belong here and are shared by planning and targeting.
class CombatMapRules {
  public:
    virtual ~CombatMapRules() = default;
    virtual bool canTraverseCardinal(Cell, Cell, TraversalCapabilities) const = 0;
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
        return std::max(std::abs(static_cast<std::int64_t>(attacker.cell.x) - target.cell.x), std::abs(static_cast<std::int64_t>(attacker.cell.z) - target.cell.z)) == 1 &&
               canPerceive(attacker, target) && (!rules || rules->canEngageMelee(attacker, target));
    }
    bool canStep(Cell from, Cell to, TraversalCapabilities capabilities,
                 std::span<const std::uint8_t> blocked) const;
};

// Both diagonal corridors stay reserved until arrival. Terrain permissions do
// not allow flying actors to overlap occupied cells.
void reserveStep(CombatMapView map, Cell from, Cell to, std::span<std::uint8_t> blocked);
Cell firstStepTowards(CombatMapView map, Actor mover, const Actor &target,
                      std::span<const std::uint8_t> blocked);

} // namespace game::arena
