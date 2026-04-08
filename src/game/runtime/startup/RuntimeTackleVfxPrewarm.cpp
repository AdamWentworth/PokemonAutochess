#include "game/runtime/startup/RuntimeTackleVfxPrewarm.h"

#include <algorithm>

#include <glm/glm.hpp>

#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxInterop.h"
#include "vfx/effects/tackle/TackleSmokeVFX.h"
#include "vfx/runtime/shared/SharedAuthoredVfxBridge.h"
#include "vfx/runtime/shared/SharedAuthoredVfxHelpers.h"
#include "vfx/runtime/shared/SharedAuthoredVfxSubmission.h"

namespace game::runtime::tackle_vfx_prewarm {

namespace {

TackleSmokeVFX::Config resolveTackleConfig() {
    TackleSmokeVFX parser;
    TackleSmokeVFX::Config config = TackleSmokeVFX::makeDefaultConfig();
    parser.setConfig(config);
    return parser.getConfig();
}

bool fillTextureView(const SharedBackendTextureCacheEntry* texture,
                     vfx::runtime::authored_batches::TextureView& outView) {
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

startup_asset_prewarm::TackleStats prewarm(const Args& args) {
    startup_asset_prewarm::TackleStats stats;
    if (!args.renderer || !args.backendTextureByPath ||
        !args.ensureBackendMeshLoaded || !args.ensureBackendTextureLoaded) {
        return stats;
    }
    if (!args.renderer->supportsWorldIndexedMeshes()) {
        return stats;
    }

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

    std::vector<vfx::runtime::authored_batches::WorldIndexedBatch> batches;
    batches.reserve(snapshot.drawPasses.size() * snapshot.rings.size());

    const auto resolveMesh =
        [&](const std::string& modelPath) -> vfx::runtime::authored_batches::MeshData* {
            render_model::MeshData* mesh = args.ensureBackendMeshLoaded(modelPath);
            if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
                return nullptr;
            }
            return const_cast<vfx::runtime::authored_batches::MeshData*>(
                &game::runtime::shared_authored_vfx_interop::cachedReusableMeshData(*mesh));
        };

    const auto resolveTexture =
        [&](const TackleSmokeVFX::Config::DrawPass& pass,
            const vfx::runtime::authored::TevState& tev,
            vfx::runtime::authored_batches::TextureView& outView) -> bool {
            if (vfx::runtime::authored::isLinePass(snapshot.config, pass) || pass.texturePath.empty()) {
                return fillTextureView(args.ensureBackendTextureLoaded("", false), outView);
            }

            SharedBackendTextureCacheEntry* rawTexture =
                args.ensureBackendTextureLoaded(pass.texturePath, false);
            if (!rawTexture || !rawTexture->valid || rawTexture->rgba.empty()) {
                return false;
            }

            const bool quarterPass =
                vfx::runtime::authored::usesQuarterTextureBake(snapshot.config, pass);
            auto& backendTextureByPath = *args.backendTextureByPath;
            if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
            const std::string bakedKey =
                vfx::runtime::authored::makeBakedTextureKey(pass, quarterPass);
            auto& baked = backendTextureByPath[bakedKey];
            if (!baked.attemptedLoad) {
                baked.attemptedLoad = true;
                baked.valid = false;
                baked.width = rawTexture->width;
                baked.height = rawTexture->height;
                baked.rgba.clear();
                if (!vfx::runtime::authored::bakePassTextureRgba(
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

    if (!vfx::runtime::authored_bridge::appendBatches(
            snapshot,
            batches,
            glm::vec3(0.0f, 0.8f, 2.5f),
            resolveMesh,
            resolveTexture)) {
        return stats;
    }

    stats.drawPasses = batches.size();
    stats.warmedBatches =
        vfx::runtime::authored_submit::prewarmBatches(*args.renderer, batches);
    return stats;
}

} // namespace game::runtime::tackle_vfx_prewarm
