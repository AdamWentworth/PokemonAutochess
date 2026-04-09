#include "game/runtime/startup/RuntimeTackleVfxPrewarm.h"

#include <algorithm>

#include <glm/glm.hpp>

#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxPrewarm.h"
#include "vfx/effects/tackle/TackleSmokeVFX.h"

namespace game::runtime::tackle_vfx_prewarm {

namespace {

TackleSmokeVFX::Config resolveTackleConfig() {
    TackleSmokeVFX parser;
    TackleSmokeVFX::Config config = TackleSmokeVFX::makeGameplayConfig();
    parser.setConfig(config);
    return parser.getConfig();
}

} // namespace

startup_asset_prewarm::TackleStats prewarm(const Args& args) {
    startup_asset_prewarm::TackleStats stats;

    TackleSmokeVFX::RenderSnapshot snapshot;
    snapshot.config = resolveTackleConfig();
    snapshot.drawPasses = snapshot.config.drawPasses;
    if (snapshot.drawPasses.empty()) {
        return stats;
    }

    const float totalLifeSec =
        std::max(snapshot.config.ringMinLifeSec, snapshot.config.ringMaxLifeSec);
    const float representativeAges[] = {
        0.00f,
        0.08f,
        0.16f,
        0.28f,
        0.56f,
    };
    snapshot.rings.reserve(std::size(representativeAges));
    for (float ageSec : representativeAges) {
        SharedAuthoredBatchVFX::RenderRing ring;
        ring.pos = glm::vec3(0.0f, 0.18f, 0.0f);
        ring.forward = glm::vec3(0.0f, 0.0f, 1.0f);
        ring.lifeSec = std::max(0.0001f, totalLifeSec);
        ring.ageSec = std::clamp(ageSec, 0.0f, ring.lifeSec);
        ring.startScale = snapshot.config.ringMinSize;
        ring.endScale = snapshot.config.ringMaxSize;
        ring.randomSeed = 0x7AC13u + static_cast<std::uint32_t>(snapshot.rings.size()) * 17u;
        snapshot.rings.push_back(ring);
    }

    const auto sharedStats = game::runtime::shared_authored_vfx_prewarm::prewarmSnapshot(
        snapshot,
        glm::vec3(0.0f, 0.8f, 2.5f),
        args);
    stats.drawPasses = sharedStats.drawPasses;
    stats.bakedTextures = sharedStats.bakedTextures;
    stats.warmedBatches = sharedStats.warmedBatches;
    return stats;
}

} // namespace game::runtime::tackle_vfx_prewarm
