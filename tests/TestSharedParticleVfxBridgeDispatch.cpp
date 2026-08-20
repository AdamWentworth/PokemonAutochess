#include <string>
#include <vector>

#include "game/runtime/shared/vfx/particles/SharedParticleVfxBridgeDispatch.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

} // namespace

bool test_shared_particle_vfx_bridge_dispatch_contract(std::string& outFail) {
    using namespace game::runtime::shared_particle_bridge_dispatch;

    GameWorld::ParticleVfxSnapshots snapshots;
    snapshots.leechSeedDrain.shaderFragPath = "drain";

    std::vector<std::string> callOrder;
    const DispatchResult result = appendStandardSnapshots(
        snapshots,
        [&](const char* label, const ParticleSystem::RenderSnapshot& snapshot) {
            callOrder.emplace_back(label ? label : "");
            if (label && std::string(label) == "leech_seed_drain") return !snapshot.shaderFragPath.empty();
            return false;
        });

    const std::vector<std::string> expected = {
        "grass_impact",
        "tackle_burst",
        "tackle_spark",
        "leech_seed_projectile",
        "leech_seed_drain",
        "heal_plus",
        "claw_swipe",
        "aqua_swoosh"
    };
    if (!expect(callOrder == expected,
                "appendStandardSnapshots should preserve shared particle append order.",
                outFail)) {
        return false;
    }
    if (!expect(result.appendedLeechDrainBillboards,
                "appendStandardSnapshots should forward the leech_seed_drain append result.",
                outFail)) {
        return false;
    }

    const DispatchResult nullResult = appendStandardSnapshots(snapshots, {});
    if (!expect(!nullResult.appendedLeechDrainBillboards,
                "appendStandardSnapshots should no-op and return false flags when callback is empty.",
                outFail)) {
        return false;
    }

    return true;
}
