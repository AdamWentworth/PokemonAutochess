#pragma once

#include "game/arena/ArenaMapData.h"

namespace game::arena {

// Board coordinates increase southward; origin maps them to authored source tiles.
class AuthoredCombatMap final : public CombatMapRules {
  public:
    AuthoredCombatMap(ArenaMapData data, Cell origin)
        : data_(std::move(data)), origin_(origin) {}
    StepKind cardinalStep(Cell from, Cell to, TraversalCapabilities capability) const override;
    bool canTraverseCardinal(Cell from, Cell to, TraversalCapabilities capability) const override {
        return cardinalStep(from, to, capability) != StepKind::Blocked;
    }
    bool canPerceive(const Actor &, const Actor &) const override { return true; }
    bool canEngageMelee(const Actor &a, const Actor &b) const override;
    const ArenaMapData &data() const { return data_; }
    Cell sourceCell(Cell board) const { return {board.x + origin_.x, board.z + origin_.z}; }

  private:
    ArenaMapData data_;
    Cell origin_;
};
} // namespace game::arena
