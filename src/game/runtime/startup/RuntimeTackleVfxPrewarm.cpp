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

void accumulate(startup_asset_prewarm::TackleStats& dst,
                const game::runtime::shared_authored_vfx_prewarm::Stats& src) {
    dst.drawPasses += src.drawPasses;
    dst.bakedTextures += src.bakedTextures;
    dst.warmedBatches += src.warmedBatches;
}

} // namespace

startup_asset_prewarm::TackleStats prewarm(const Args& args) {
    startup_asset_prewarm::TackleStats stats;

    const TackleSmokeVFX::Config config = resolveTackleConfig();
    if (config.drawPasses.empty()) {
        return stats;
    }

    const float totalLifeSec = std::max(config.ringMinLifeSec, config.ringMaxLifeSec);
    const float representativeAges[] = {
        0.00f,
        1.0f / 60.0f,
        0.08f,
        0.16f,
        0.28f,
        0.56f,
    };

    std::uint32_t ageIndex = 0u;
    for (float ageSec : representativeAges) {
        TackleSmokeVFX::RenderSnapshot snapshot;
        snapshot.config = config;
        snapshot.drawPasses = config.drawPasses;

        SharedAuthoredBatchVFX::RenderRing ring;
        ring.pos = glm::vec3(0.0f, 0.18f, 0.0f);
        ring.forward = glm::vec3(0.0f, 0.0f, 1.0f);
        ring.lifeSec = std::max(0.0001f, totalLifeSec);
        ring.ageSec = std::clamp(ageSec, 0.0f, ring.lifeSec);
        ring.startScale = snapshot.config.ringMinSize;
        ring.endScale = snapshot.config.ringMaxSize;
        ring.randomSeed = 0x7AC13u + ageIndex * 17u;
        snapshot.rings.push_back(ring);

        accumulate(
            stats,
            game::runtime::shared_authored_vfx_prewarm::prewarmSnapshot(
                snapshot,
                glm::vec3(0.0f, 0.8f, 2.5f),
                args));
        ++ageIndex;
    }
    return stats;
}

} // namespace game::runtime::tackle_vfx_prewarm
