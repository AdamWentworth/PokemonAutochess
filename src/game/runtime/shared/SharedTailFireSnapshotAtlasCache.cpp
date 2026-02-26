#include "game/runtime/shared/SharedTailFireSnapshotAtlasCache.h"

#include <algorithm>

#include "game/runtime/shared/SharedTailFireAtlasHelpers.h"
#include "game/runtime/shared/SharedTailFireExactGpuBatches.h"
#include "game/runtime/shared/SharedTailFireSnapshotBillboards.h"

namespace game::runtime::shared_tail_fire_snapshot_billboards {
namespace {

using BackendTextureCacheEntry = SharedBackendTextureCacheEntry;
using WorldIndexedBatch = shared_world_batches::WorldIndexedBatch;

} // namespace

BackendTextureCacheEntry* resolveTailFirePremulAtlas(
    const std::string& atlasPath,
    std::unordered_map<std::string, BackendTextureCacheEntry>& backendTextureByPath,
    const std::function<BackendTextureCacheEntry*(const std::string&, bool)>& ensureTextureFn) {
    if (atlasPath.empty()) return nullptr;
    if (!ensureTextureFn) return nullptr;

    // Legacy ParticleSystem loads VFX flipbooks with stb vertical flip enabled.
    // Match that texture orientation here so the shared fire_tail UV logic aligns.
    BackendTextureCacheEntry* src = ensureTextureFn(atlasPath, true);
    if (!src || !src->valid || src->rgba.empty() || src->width <= 0 || src->height <= 0) {
        return nullptr;
    }

    const std::string key = std::string("__tailfire_premul:") + atlasPath;
    if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
    auto& baked = backendTextureByPath[key];
    if (baked.attemptedLoad) return baked.valid ? &baked : nullptr;

    baked.attemptedLoad = true;
    baked.valid = false;
    game::runtime::shared_tail_fire_atlas::RgbaTextureOwned premul;
    const game::runtime::shared_tail_fire_atlas::RgbaTextureView srcView{
        src->rgba.data(), src->width, src->height};
    if (!game::runtime::shared_tail_fire_atlas::buildPremultipliedAtlas(srcView, premul)) {
        return nullptr;
    }

    baked.width = premul.width;
    baked.height = premul.height;
    baked.rgba = std::move(premul.rgba);
    baked.valid = (baked.width > 0 && baked.height > 0 && !baked.rgba.empty());
    return &baked;
}

TailFireCombinedAtlasInfo resolveTailFireCombinedAtlas(
    const ParticleSystem::RenderSnapshot& snapshot,
    std::unordered_map<std::string, BackendTextureCacheEntry>& backendTextureByPath,
    const std::function<BackendTextureCacheEntry*(const std::string&, bool)>& ensureTextureFn) {
    TailFireCombinedAtlasInfo out;
    if (!snapshot.useFlipbook || snapshot.flipbookPath.empty()) return out;
    if (!ensureTextureFn) return out;

    BackendTextureCacheEntry* primaryRaw = ensureTextureFn(snapshot.flipbookPath, true);
    if (!primaryRaw || !primaryRaw->valid || primaryRaw->rgba.empty() ||
        primaryRaw->width <= 0 || primaryRaw->height <= 0) {
        return out;
    }

    BackendTextureCacheEntry* secondaryRaw = nullptr;
    if (snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty()) {
        secondaryRaw = ensureTextureFn(snapshot.flipbookPath2, true);
        if (!(secondaryRaw && secondaryRaw->valid && !secondaryRaw->rgba.empty() &&
              secondaryRaw->width > 0 && secondaryRaw->height > 0)) {
            secondaryRaw = nullptr;
        }
    }

    out.hasSecondary = (secondaryRaw != nullptr);
    out.cacheKey = std::string("__tailfire_combined_exact__:") +
                   snapshot.flipbookPath +
                   "|" +
                   (secondaryRaw ? snapshot.flipbookPath2 : std::string());
    if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
    auto& combined = backendTextureByPath[out.cacheKey];
    if (!combined.attemptedLoad) {
        combined.attemptedLoad = true;
        combined.valid = false;
        game::runtime::shared_tail_fire_atlas::RgbaTextureOwned builtAtlas;
        game::runtime::shared_tail_fire_atlas::CombinedAtlasInfo builtInfo;
        const game::runtime::shared_tail_fire_atlas::RgbaTextureView primaryView{
            primaryRaw->rgba.data(), primaryRaw->width, primaryRaw->height};
        game::runtime::shared_tail_fire_atlas::RgbaTextureView secondaryView{};
        const game::runtime::shared_tail_fire_atlas::RgbaTextureView* secondaryViewPtr = nullptr;
        if (out.hasSecondary) {
            secondaryView = {secondaryRaw->rgba.data(), secondaryRaw->width, secondaryRaw->height};
            secondaryViewPtr = &secondaryView;
        }
        if (game::runtime::shared_tail_fire_atlas::buildCombinedAtlas(
                primaryView, secondaryViewPtr, builtAtlas, builtInfo)) {
            combined.width = builtAtlas.width;
            combined.height = builtAtlas.height;
            combined.rgba = std::move(builtAtlas.rgba);
            combined.valid = (combined.width > 0 && combined.height > 0 && !combined.rgba.empty());
            out.hasSecondary = builtInfo.hasSecondary;
            out.rect0 = builtInfo.rect0;
            out.rect1 = builtInfo.rect1;
        }
    }

    if (!combined.valid || combined.rgba.empty() || combined.width <= 0 || combined.height <= 0) {
        return {};
    }

    out.atlas = &combined;
    if (out.rect0 == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) &&
        out.rect1 == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)) {
        const float invW = 1.0f / static_cast<float>(std::max(1, combined.width));
        const float invH = 1.0f / static_cast<float>(std::max(1, combined.height));
        out.rect0 = glm::vec4(
            0.0f,
            0.0f,
            static_cast<float>(primaryRaw->width) * invW,
            static_cast<float>(primaryRaw->height) * invH);
        if (out.hasSecondary) {
            const int gutter = 2;
            out.rect1 = glm::vec4(
                static_cast<float>(primaryRaw->width + gutter) * invW,
                0.0f,
                static_cast<float>(secondaryRaw->width) * invW,
                static_cast<float>(secondaryRaw->height) * invH);
        } else {
            out.rect1 = out.rect0;
        }
    }
    return out;
}

bool appendTailFireExactGpuBatch(
    const char* label,
    const ParticleSystem::RenderSnapshot& snapshot,
    std::uint8_t blendMode,
    const AppendContext& ctx,
    std::vector<WorldIndexedBatch>& worldIndexedBatches) {
    if (!label || snapshot.particles.empty()) return false;
    if (!snapshot.useFlipbook || snapshot.flipbookPath.empty()) return false;

    TailFireCombinedAtlasInfo atlasInfo =
        resolveTailFireCombinedAtlas(snapshot, ctx.backendTextureByPath, ctx.ensureTextureFn);
    if (!atlasInfo.atlas || !atlasInfo.atlas->valid || atlasInfo.atlas->rgba.empty()) {
        return false;
    }

    game::runtime::shared_tail_fire_exact_gpu::BuildContext tailCtx;
    tailCtx.viewProj = ctx.viewProj;
    tailCtx.invViewProj = ctx.invViewProj;
    tailCtx.cameraWorldPos = ctx.cameraWorldPos;
    tailCtx.drawableW = ctx.drawableW;
    tailCtx.drawableH = ctx.drawableH;
    tailCtx.blendMode = blendMode;

    game::runtime::shared_tail_fire_exact_gpu::AtlasView tailAtlas;
    tailAtlas.rgba = atlasInfo.atlas->rgba.data();
    tailAtlas.width = atlasInfo.atlas->width;
    tailAtlas.height = atlasInfo.atlas->height;
    tailAtlas.cacheKey = atlasInfo.cacheKey;
    tailAtlas.rect0 = atlasInfo.rect0;
    tailAtlas.rect1 = atlasInfo.rect1;
    tailAtlas.hasSecondary = atlasInfo.hasSecondary;

    return game::runtime::shared_tail_fire_exact_gpu::appendBatch(
        label, snapshot, tailCtx, tailAtlas, worldIndexedBatches);
}

} // namespace game::runtime::shared_tail_fire_snapshot_billboards
