#include "game/runtime/shared/vfx/particles/SharedParticleVfxBridgeDispatch.h"

namespace game::runtime::shared_particle_bridge_dispatch {

DispatchResult appendStandardSnapshots(const GameWorld::ParticleVfxSnapshots& snapshots,
                                       const AppendSnapshotFn& appendSnapshot) {
    DispatchResult result;
    if (!appendSnapshot) return result;

    appendSnapshot("grass_impact", snapshots.grassImpact);
    appendSnapshot("tackle_burst", snapshots.tackleBurst);
    appendSnapshot("tackle_spark", snapshots.tackleSpark);
    appendSnapshot("leech_seed_projectile", snapshots.leechSeedProjectile);
    result.appendedLeechDrainBillboards =
        appendSnapshot("leech_seed_drain", snapshots.leechSeedDrain);
    appendSnapshot("heal_plus", snapshots.healPlus);
    appendSnapshot("claw_swipe", snapshots.clawSwipe);
    appendSnapshot("aqua_swoosh", snapshots.aquaSwoosh);

    return result;
}

} // namespace game::runtime::shared_particle_bridge_dispatch
