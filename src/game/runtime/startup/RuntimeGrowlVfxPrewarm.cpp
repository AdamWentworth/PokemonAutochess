#include "game/runtime/startup/RuntimeGrowlVfxPrewarm.h"

#include "engine/render/Model.h"
#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxPrewarm.h"
#include "vfx/effects/growl/GrowlWaveVFX.h"

#include <glm/glm.hpp>

namespace game::runtime::growl_vfx_prewarm {

namespace {

GrowlWaveVFX::Config resolveGrowlConfig() {
    GrowlWaveVFX parser;
    GrowlWaveVFX::Config config;
    config.spawnForwardOffset = 0.0f;
    config.spawnHeightOffset = 0.0f;
    config.drawManifestPath = "config/vfx/moves/growl_draw_passes.json";
    parser.setConfig(config);
    return parser.getConfig();
}

} // namespace

startup_asset_prewarm::GrowlStats prewarm(const Args& args) {
    startup_asset_prewarm::GrowlStats stats;

    GrowlWaveVFX::RenderSnapshot snapshot;
    snapshot.config = resolveGrowlConfig();
    snapshot.drawPasses = snapshot.config.drawPasses;
    if (snapshot.drawPasses.empty()) {
        return stats;
    }

    GrowlWaveVFX::RenderRing ring;
    ring.pos = glm::vec3(0.0f, 0.18f, 0.0f);
    ring.forward = glm::vec3(0.0f, 0.0f, 1.0f);
    ring.lifeSec = 0.8f;
    ring.ageSec = 0.0f;
    ring.startScale = 1.0f;
    ring.endScale = 1.35f;
    ring.randomSeed = 0x47A11u;
    snapshot.rings.push_back(ring);

    const auto sharedStats = game::runtime::shared_authored_vfx_prewarm::prewarmSnapshot(
        snapshot,
        glm::vec3(0.0f, 0.8f, 2.5f),
        args);
    stats.drawPasses = sharedStats.drawPasses;
    stats.bakedTextures = sharedStats.bakedTextures;
    stats.warmedBatches = sharedStats.warmedBatches;
    return stats;
}

} // namespace game::runtime::growl_vfx_prewarm
