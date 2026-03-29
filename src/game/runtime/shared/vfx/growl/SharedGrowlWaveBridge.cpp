#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBridge.h"

namespace game::runtime::shared_growl_bridge {

bool appendBatches(const GrowlWaveVFX::RenderSnapshot& snapshot,
                   std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
                   const glm::vec3& cameraWorldPos,
                   const MeshResolver& resolveMesh,
                   const TextureResolver& resolveTexture) {
    if (snapshot.drawPasses.empty() || snapshot.rings.empty()) return false;

    bool appendedAny = false;
    for (const auto& pass : snapshot.drawPasses) {
        if (!pass.enabled) continue;

        const shared_growl::TevState passTev = shared_growl::resolveTevState(snapshot.config, pass);

        const bool drawQuarterRing = pass.textureQuarterRing;
        const bool glowBillboardPass = shared_growl::isGlowBillboardPass(pass);
        render_model::MeshData* passMesh = nullptr;
        if (!drawQuarterRing && !glowBillboardPass) {
            if (pass.meshPath.empty()) continue;
            if (!resolveMesh) continue;
            passMesh = resolveMesh(pass.meshPath);
            if (!passMesh) continue;
        }

        if (!resolveTexture) continue;
        shared_growl_batches::TextureView texture;
        if (!resolveTexture(pass, passTev, texture)) continue;

        appendedAny =
            shared_growl_batches::appendPassBatch(outBatches,
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

} // namespace game::runtime::shared_growl_bridge

