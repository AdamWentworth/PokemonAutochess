#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxPrewarm.h"

#include <algorithm>
#include <vector>

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxInterop.h"
#include "vfx/runtime/shared/SharedAuthoredVfxBridge.h"
#include "vfx/runtime/shared/SharedAuthoredVfxHelpers.h"
#include "vfx/runtime/shared/SharedAuthoredVfxSubmission.h"

namespace game::runtime::shared_authored_vfx_prewarm {

namespace {

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

Stats prewarmSnapshot(const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
                      const glm::vec3& cameraWorldPos,
                      const Args& args) {
    Stats stats;
    if (!args.renderer || !args.backendTextureByPath ||
        !args.ensureBackendMeshLoaded || !args.ensureBackendTextureLoaded) {
        return stats;
    }
    if (!args.renderer->supportsWorldIndexedMeshes()) {
        return stats;
    }
    if (snapshot.drawPasses.empty() || snapshot.rings.empty()) {
        return stats;
    }

    std::vector<vfx::runtime::authored_batches::WorldIndexedBatch> batches;
    batches.reserve(snapshot.drawPasses.size() *
                    std::max<std::size_t>(snapshot.rings.size(), 1u));

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
        [&](const SharedAuthoredBatchVFX::Config::DrawPass& pass,
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
            bool bakedThisPass = false;
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
                bakedThisPass = true;
            }

            if (!fillTextureView(&baked, outView)) {
                return false;
            }

            if (bakedThisPass) ++stats.bakedTextures;
            return true;
        };

    if (!vfx::runtime::authored_bridge::appendBatches(
            snapshot,
            batches,
            cameraWorldPos,
            resolveMesh,
            resolveTexture)) {
        return stats;
    }

    game::runtime::shared_authored_vfx_interop::mergeCompatibleInstancedAdditiveBatches(
        batches);
    stats.drawPasses = batches.size();
    stats.warmedBatches =
        vfx::runtime::authored_submit::prewarmBatches(*args.renderer, batches);
    return stats;
}

} // namespace game::runtime::shared_authored_vfx_prewarm
