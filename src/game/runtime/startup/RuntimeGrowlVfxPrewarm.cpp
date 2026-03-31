#include "game/runtime/startup/RuntimeGrowlVfxPrewarm.h"

#include "engine/render/Model.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlInterop.h"
#include "vfx/runtime/growl/SharedGrowlVfxHelpers.h"
#include "vfx/runtime/growl/SharedGrowlBatchSubmission.h"
#include "vfx/runtime/growl/SharedGrowlWaveBridge.h"
#include "vfx/runtime/growl/SharedGrowlWaveBatches.h"
#include "vfx/effects/growl/GrowlWaveVFX.h"

#include <vector>
#include <unordered_map>

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

bool fillTextureView(const SharedBackendTextureCacheEntry* texture,
                     vfx::runtime::growl_batches::TextureView& outView) {
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

    std::vector<vfx::runtime::growl_batches::WorldIndexedBatch> batches;
    batches.reserve(snapshot.drawPasses.size());
    std::unordered_map<std::string, vfx::runtime::growl_batches::MeshData> growlMeshesByPath;
    growlMeshesByPath.reserve(snapshot.drawPasses.size());

    const auto resolveMesh =
        [&](const std::string& modelPath) -> vfx::runtime::growl_batches::MeshData* {
            auto found = growlMeshesByPath.find(modelPath);
            if (found != growlMeshesByPath.end()) {
                return &found->second;
            }

            render_model::MeshData* mesh = args.ensureBackendMeshLoaded(modelPath);
            if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
                return nullptr;
            }

            auto inserted = growlMeshesByPath.emplace(
                modelPath,
                game::runtime::shared_growl_interop::toReusableMeshData(*mesh));
            return &inserted.first->second;
        };

    const auto resolveTexture =
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const vfx::runtime::growl::TevState& tev,
            vfx::runtime::growl_batches::TextureView& outView) -> bool {
            if (vfx::runtime::growl::isLinePass(snapshot.config, pass) || pass.texturePath.empty()) {
                return fillTextureView(args.ensureBackendTextureLoaded("", false), outView);
            }

            SharedBackendTextureCacheEntry* rawTexture =
                args.ensureBackendTextureLoaded(pass.texturePath, false);
            if (!rawTexture || !rawTexture->valid || rawTexture->rgba.empty()) {
                return false;
            }

            const bool quarterPass =
                vfx::runtime::growl::isQuarterRingPass(snapshot.config, pass);
            auto& backendTextureByPath = *args.backendTextureByPath;
            if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
            const std::string bakedKey =
                vfx::runtime::growl::makeBakedTextureKey(pass, quarterPass);
            auto& baked = backendTextureByPath[bakedKey];
            if (!baked.attemptedLoad) {
                baked.attemptedLoad = true;
                baked.valid = false;
                baked.width = rawTexture->width;
                baked.height = rawTexture->height;
                baked.rgba.clear();
                if (!vfx::runtime::growl::bakePassTextureRgba(
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

    if (!vfx::runtime::growl_bridge::appendBatches(
            snapshot,
            batches,
            glm::vec3(0.0f, 0.8f, 2.5f),
            resolveMesh,
            resolveTexture)) {
        return stats;
    }

    stats.drawPasses = batches.size();
    stats.warmedBatches =
        vfx::runtime::growl_submit::prewarmBatches(*args.renderer, batches);
    return stats;
}

} // namespace game::runtime::growl_vfx_prewarm
