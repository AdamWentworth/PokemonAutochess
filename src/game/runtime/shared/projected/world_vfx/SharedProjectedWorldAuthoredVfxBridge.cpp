#include "game/runtime/shared/projected/world_vfx/SharedProjectedWorldAuthoredVfxBridge.h"

#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxInterop.h"
#include "vfx/runtime/shared/SharedAuthoredVfxBridge.h"
#include "vfx/runtime/shared/SharedAuthoredVfxHelpers.h"

namespace game::runtime::shared_projected_authored_vfx_bridge {

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

bool appendSnapshot(const Args& args) {
    if (!args.snapshot || !args.backendTextureByPath || !args.worldIndexedBatches) return false;
    if (!args.ensureBackendMeshLoaded || !args.ensureBackendTextureLoaded) return false;
    if (args.snapshot->drawPasses.empty() || args.snapshot->rings.empty()) return false;

    using AuthoredTevState = vfx::runtime::authored::TevState;

    const auto resolveSharedTexture =
        [&](const SharedAuthoredBatchVFX::Config::DrawPass& pass,
            const AuthoredTevState& tev) -> SharedBackendTextureCacheEntry* {
            if (vfx::runtime::authored::isLinePass(args.snapshot->config, pass) ||
                pass.texturePath.empty()) {
                return args.ensureBackendTextureLoaded("", false);
            }

            SharedBackendTextureCacheEntry* rawTex =
                args.ensureBackendTextureLoaded(pass.texturePath, false);
            if (!rawTex || !rawTex->valid || rawTex->rgba.empty() ||
                rawTex->width <= 0 || rawTex->height <= 0) {
                return nullptr;
            }

            const bool quarterPass =
                vfx::runtime::authored::isQuarterRingPass(args.snapshot->config, pass);
            auto& backendTextureByPath = *args.backendTextureByPath;
            if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
            const std::string bakedKey =
                vfx::runtime::authored::makeBakedTextureKey(pass, quarterPass);
            auto& baked = backendTextureByPath[bakedKey];
            if (baked.attemptedLoad) {
                return baked.valid ? &baked : nullptr;
            }

            baked.attemptedLoad = true;
            baked.valid = false;
            baked.width = rawTex->width;
            baked.height = rawTex->height;
            baked.rgba.clear();
            if (!vfx::runtime::authored::bakePassTextureRgba(
                    pass, tev, quarterPass, rawTex->rgba, baked.rgba)) {
                return nullptr;
            }

            baked.valid = true;
            return &baked;
        };

    const auto resolveTextureView =
        [&](const SharedAuthoredBatchVFX::Config::DrawPass& pass,
            const AuthoredTevState& tev,
            vfx::runtime::authored_batches::TextureView& outView) {
            SharedBackendTextureCacheEntry* tex = resolveSharedTexture(pass, tev);
            if (!tex) tex = args.ensureBackendTextureLoaded("", false);
            return fillTextureView(tex, outView);
        };

    const auto resolveMesh =
        [&](const std::string& modelPath) -> vfx::runtime::authored_batches::MeshData* {
            runtime::render_model::MeshData* mesh = args.ensureBackendMeshLoaded(modelPath);
            if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
                return nullptr;
            }
            return const_cast<vfx::runtime::authored_batches::MeshData*>(
                &game::runtime::shared_authored_vfx_interop::cachedReusableMeshData(*mesh));
        };

    std::vector<vfx::runtime::authored_batches::WorldIndexedBatch> authoredBatches;
    authoredBatches.reserve(args.snapshot->drawPasses.size() *
                            std::max<std::size_t>(args.snapshot->rings.size(), 1u) *
                            std::max<std::size_t>(args.reserveMultiplier, 1u));
    if (!vfx::runtime::authored_bridge::appendBatches(
            *args.snapshot,
            authoredBatches,
            args.cameraWorldPos,
            resolveMesh,
            resolveTextureView)) {
        return false;
    }

    game::runtime::shared_authored_vfx_interop::appendWorldIndexedBatches(
        authoredBatches,
        *args.worldIndexedBatches);
    return !authoredBatches.empty();
}

} // namespace game::runtime::shared_projected_authored_vfx_bridge
