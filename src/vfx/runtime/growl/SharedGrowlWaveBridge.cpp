#include "vfx/runtime/growl/SharedGrowlWaveBridge.h"

namespace vfx::runtime::growl_bridge {

namespace render_model = game::runtime::render_model;
namespace shared_world_batches = game::runtime::shared_world_batches;

bool appendBatches(const GrowlWaveVFX::RenderSnapshot& snapshot,
                   std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
                   const glm::vec3& cameraWorldPos,
                   const MeshResolver& resolveMesh,
                   const TextureResolver& resolveTexture) {
    if (snapshot.drawPasses.empty() || snapshot.rings.empty()) return false;

    bool appendedAny = false;
    for (const auto& pass : snapshot.drawPasses) {
        if (!pass.enabled) continue;

        const growl::TevState passTev = growl::resolveTevState(snapshot.config, pass);

        const bool drawQuarterRing = pass.textureQuarterRing;
        const bool glowBillboardPass = growl::isGlowBillboardPass(pass);
        render_model::MeshData* passMesh = nullptr;
        if (!drawQuarterRing && !glowBillboardPass) {
            if (pass.meshPath.empty()) continue;
            if (!resolveMesh) continue;
            passMesh = resolveMesh(pass.meshPath);
            if (!passMesh) continue;
        }

        if (!resolveTexture) continue;
        growl_batches::TextureView texture;
        if (!resolveTexture(pass, passTev, texture)) continue;

        appendedAny =
            growl_batches::appendPassBatch(outBatches,
                                           snapshot,
                                           pass,
                                           passTev,
                                           passMesh,
                                           texture,
                                           cameraWorldPos) ||
            appendedAny;
    }

    return appendedAny;
}

} // namespace vfx::runtime::growl_bridge

