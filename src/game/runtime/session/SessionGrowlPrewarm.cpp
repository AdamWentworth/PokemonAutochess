#include "game/runtime/session/SessionGrowlPrewarm.h"

#include "engine/render/Model.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlVfxHelpers.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBridge.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBatches.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "game/vfx/GrowlWaveVFX.h"

#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::session_growl_prewarm {

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

bool fillTextureView(const SharedBackendTextureCacheEntry* texture,
                     shared_growl_batches::TextureView& outView) {
    if (!texture || !texture->valid || texture->rgba.empty() ||
        texture->width <= 0 || texture->height <= 0) {
        return false;
    }
    outView.rgba = texture->rgba.data();
    outView.width = texture->width;
    outView.height = texture->height;
    return true;
}

} // namespace

startup_asset_prewarm::GrowlStats prewarm(const Args& args) {
    startup_asset_prewarm::GrowlStats stats;
    if (!args.renderer || !args.backendTextureByPath ||
        !args.ensureBackendMeshLoaded || !args.ensureBackendTextureLoaded) {
        return stats;
    }
    if (!args.renderer->supportsWorldIndexedMeshes()) {
        return stats;
    }

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

    std::vector<shared_world_batches::WorldIndexedBatch> batches;
    batches.reserve(snapshot.drawPasses.size());

    const auto resolveMesh =
        [&](const std::string& modelPath) -> render_model::MeshData* {
            return args.ensureBackendMeshLoaded(modelPath);
        };

    const auto resolveTexture =
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const shared_growl::TevState& tev,
            shared_growl_batches::TextureView& outView) -> bool {
            if (shared_growl::isLinePass(snapshot.config, pass) || pass.texturePath.empty()) {
                return fillTextureView(args.ensureBackendTextureLoaded("", false), outView);
            }

            SharedBackendTextureCacheEntry* rawTexture =
                args.ensureBackendTextureLoaded(pass.texturePath, false);
            if (!rawTexture || !rawTexture->valid || rawTexture->rgba.empty()) {
                return false;
            }

            const bool quarterPass =
                shared_growl::isQuarterRingPass(snapshot.config, pass);
            auto& backendTextureByPath = *args.backendTextureByPath;
            if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
            const std::string bakedKey =
                shared_growl::makeBakedTextureKey(pass, quarterPass);
            auto& baked = backendTextureByPath[bakedKey];
            if (!baked.attemptedLoad) {
                baked.attemptedLoad = true;
                baked.valid = false;
                baked.width = rawTexture->width;
                baked.height = rawTexture->height;
                baked.rgba.clear();
                if (!shared_growl::bakePassTextureRgba(
                        pass,
                        tev,
                        quarterPass,
                        rawTexture->rgba,
                        baked.rgba)) {
                    return false;
                }
                baked.valid = true;
            }

            if (!fillTextureView(&baked, outView)) {
                return false;
            }

            ++stats.bakedTextures;
            return true;
        };

    if (!shared_growl_bridge::appendBatches(
            snapshot,
            batches,
            glm::vec3(0.0f, 0.8f, 2.5f),
            resolveMesh,
            resolveTexture)) {
        return stats;
    }

    stats.drawPasses = batches.size();
    stats.warmedBatches =
        shared_world_batches::prewarmWorldIndexedBatches(*args.renderer, batches);
    return stats;
}

} // namespace game::runtime::session_growl_prewarm
