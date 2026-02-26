#pragma once

#include <functional>

#include "game/world/GameWorld.h"

namespace game::runtime::shared_particle_bridge_dispatch {

struct DispatchResult {
    bool appendedTailFireBillboards = false;
    bool appendedLeechDrainBillboards = false;
};

using AppendSnapshotFn =
    std::function<bool(const char* label, const ParticleSystem::RenderSnapshot& snapshot)>;

DispatchResult appendStandardSnapshots(const GameWorld::ParticleVfxSnapshots& snapshots,
                                       const AppendSnapshotFn& appendSnapshot);

} // namespace game::runtime::shared_particle_bridge_dispatch
