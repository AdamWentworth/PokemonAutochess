#include "game/runtime/startup/RuntimeScratchVfxPrewarm.h"

#include <glm/glm.hpp>

#include "vfx/effects/scratch/ScratchGlowVFX.h"

namespace game::runtime::scratch_vfx_prewarm {

namespace {

constexpr int kScratchAuthoredFps = 30;
constexpr int kScratchVisibleFrames = 13;

void accumulate(startup_asset_prewarm::ScratchStats& dst,
                const game::runtime::shared_authored_vfx_prewarm::Stats& src) {
    dst.drawPasses += src.drawPasses;
    dst.bakedTextures += src.bakedTextures;
    dst.warmedBatches += src.warmedBatches;
}

} // namespace

startup_asset_prewarm::ScratchStats prewarm(const Args& args) {
    startup_asset_prewarm::ScratchStats stats;

    ScratchGlowVFX scratch;
    scratch.setConfig(ScratchGlowVFX::makeGameplayConfig());
    scratch.emitAt(glm::vec3(0.0f, 0.18f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    for (int frame = 0; frame < kScratchVisibleFrames; ++frame) {
        ScratchGlowVFX::RenderSnapshot snapshot;
        if (scratch.buildRenderSnapshot(snapshot)) {
            accumulate(
                stats,
                game::runtime::shared_authored_vfx_prewarm::prewarmSnapshot(
                    snapshot,
                    glm::vec3(0.0f, 0.8f, 2.5f),
                    args));
        }
        scratch.update(1.0f / static_cast<float>(kScratchAuthoredFps));
    }
    return stats;
}

} // namespace game::runtime::scratch_vfx_prewarm
