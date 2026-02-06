// src/engine/core/ecs/Scheduler.h

#pragma once
#include "engine/core/ecs/ISystem.h"
#include <vector>
#include <memory>

namespace engine::ecs {

// Minimal scheduler: ordered list of systems.
// Game will own which systems exist + ordering; engine provides the mechanism.
class Scheduler {
public:
    void add(std::unique_ptr<ISystem> sys) { systems_.push_back(std::move(sys)); }

    void tick(World& world, float dt) {
        for (auto& s : systems_) s->update(world, dt);
    }

    std::size_t size() const { return systems_.size(); }

private:
    std::vector<std::unique_ptr<ISystem>> systems_;
};

} // namespace engine::ecs
