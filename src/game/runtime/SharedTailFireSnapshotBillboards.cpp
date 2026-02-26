#include "game/runtime/SharedTailFireSnapshotBillboards.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/SharedTailFireAtlasHelpers.h"
#include "game/runtime/SharedTailFireExactCpuSnapshotBatches.h"
#include "game/runtime/SharedTailFireExactGpuBatches.h"

namespace game::runtime::shared_tail_fire_snapshot_billboards {
namespace {

using BackendTextureCacheEntry = SharedBackendTextureCacheEntry;
using WorldIndexedBatch = shared_world_batches::WorldIndexedBatch;

std::uint8_t toBackendBlendMode(ParticleSystem::BlendMode mode) {
    switch (mode) {
    case ParticleSystem::BlendMode::Additive:
        return 1u;
    case ParticleSystem::BlendMode::Premultiplied:
        return 2u;
    case ParticleSystem::BlendMode::Alpha:
    default:
        return 0u;
    }
}

bool safeUnprojectClip(const glm::mat4& invViewProj,
                       const glm::vec4& clipPos,
                       glm::vec3& outWorld) {
    const glm::vec4 world = invViewProj * clipPos;
    if (!std::isfinite(world.x) || !std::isfinite(world.y) ||
        !std::isfinite(world.z) || !std::isfinite(world.w)) {
        return false;
    }
    if (std::fabs(world.w) <= 0.000001f) return false;
    outWorld = glm::vec3(world) / world.w;
    return std::isfinite(outWorld.x) && std::isfinite(outWorld.y) &&
           std::isfinite(outWorld.z);
}

float hashFrac01(float x) {
    const float s = std::sin(x * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

glm::vec3 tailFireRampOrangeRed(float age01) {
    const glm::vec3 hot(1.05f, 0.42f, 0.18f);
    const glm::vec3 mid(0.90f, 0.30f, 0.14f);
    const glm::vec3 cool(0.58f, 0.16f, 0.10f);
    glm::vec3 c = glm::mix(hot, mid, glm::smoothstep(0.0f, 0.65f, age01));
    c = glm::mix(c, cool, glm::smoothstep(0.55f, 1.0f, age01));
    c *= 0.70f;
    const float l = glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    c = glm::mix(glm::vec3(l), c, 0.95f);
    return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
}

struct TailFireCombinedAtlasInfo {
    BackendTextureCacheEntry* atlas = nullptr;
    std::string cacheKey;
    glm::vec4 rect0{0.0f, 0.0f, 1.0f, 1.0f};
    glm::vec4 rect1{0.0f, 0.0f, 1.0f, 1.0f};
    bool hasSecondary = false;
};

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

} // namespace

bool appendTailFireSnapshotBillboards(
    const char* label,
    const ParticleSystem::RenderSnapshot& snapshot,
    std::uint8_t blendMode,
    const AppendContext& ctx,
    std::vector<WorldIndexedBatch>& worldIndexedBatches) {
    if (!label) return false;
    if (snapshot.particles.empty()) return false;

    if (appendTailFireExactGpuBatch(label, snapshot, blendMode, ctx, worldIndexedBatches)) {
        return true;
    }

    if (!(snapshot.useFlipbook && !snapshot.flipbookPath.empty())) {
        return false;
    }

    BackendTextureCacheEntry* primaryTex =
        resolveTailFirePremulAtlas(snapshot.flipbookPath, ctx.backendTextureByPath, ctx.ensureTextureFn);
    BackendTextureCacheEntry* secondaryTex =
        (snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty())
            ? resolveTailFirePremulAtlas(snapshot.flipbookPath2, ctx.backendTextureByPath, ctx.ensureTextureFn)
            : nullptr;
    if (!primaryTex || !primaryTex->valid || primaryTex->rgba.empty()) {
        return false;
    }

    BackendTextureCacheEntry* primaryRawTex = ctx.ensureTextureFn
                                                  ? ctx.ensureTextureFn(snapshot.flipbookPath, true)
                                                  : nullptr;
    BackendTextureCacheEntry* secondaryRawTex =
        (ctx.ensureTextureFn && snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty())
            ? ctx.ensureTextureFn(snapshot.flipbookPath2, true)
            : nullptr;

    if (ctx.tailFireExactCpuEnabled &&
        game::runtime::shared_tail_fire_exact_cpu_snapshot::appendExactBatch(
            label,
            snapshot,
            ctx.viewProj,
            ctx.invViewProj,
            ctx.cameraWorldPos,
            ctx.drawableW,
            ctx.drawableH,
            blendMode,
            primaryRawTex,
            secondaryRawTex,
            worldIndexedBatches)) {
        return true;
    }

    auto initParticleBatch = [&](WorldIndexedBatch& batch,
                                 const char* passName,
                                 const std::string& texPath,
                                 const BackendTextureCacheEntry& texRef) {
        batch = {};
        batch.textureKey = std::string("particle:") + label + ":" + passName + ":" + texPath;
        batch.textureRgba = texRef.rgba.data();
        batch.textureWidth = texRef.width;
        batch.textureHeight = texRef.height;
        batch.textureWrapS = 33071; // clamp
        batch.textureWrapT = 33071; // clamp
        batch.alphaMode = 2u;
        batch.blendMode = blendMode;
        batch.alphaCutoff = 0.0f;
        batch.sortDepth = 0.0f;
        batch.vertices.reserve(snapshot.particles.size() * 4u);
        batch.indices.reserve(snapshot.particles.size() * 6u);
    };

    WorldIndexedBatch hybridBatch;
    initParticleBatch(hybridBatch, "tail_fire_hybrid", snapshot.flipbookPath, *primaryTex);

    WorldIndexedBatch coreBatch;
    const bool hasSecondary = (secondaryTex && secondaryTex->valid && !secondaryTex->rgba.empty());
    if (hasSecondary) {
        initParticleBatch(coreBatch, "tail_fire_core", snapshot.flipbookPath2, *secondaryTex);
    }

    auto computeTailFireFrameUv = [&](const ParticleSystem::Particle& particle,
                                      bool secondary,
                                      float& u0,
                                      float& v0,
                                      float& u1,
                                      float& v1) {
        const int cols = std::max(1, secondary ? snapshot.flipbookCols2 : snapshot.flipbookCols);
        const int rows = std::max(1, secondary ? snapshot.flipbookRows2 : snapshot.flipbookRows);
        const int maxFrames = std::max(1, cols * rows);
        const int frameCountRaw = secondary ? snapshot.flipbookFrames2 : snapshot.flipbookFrames;
        const int frames = std::clamp(frameCountRaw, 1, maxFrames);
        const float fps = std::max(0.0f, secondary ? snapshot.flipbookFps2 : snapshot.flipbookFps);
        if (frames <= 1 || cols <= 0 || rows <= 0 || fps <= 0.0f) {
            u0 = 0.0f;
            v0 = 0.0f;
            u1 = 1.0f;
            v1 = 1.0f;
            return;
        }

        const float seed = std::clamp(particle.seed, 0.0f, 1.0f);
        const float speedNoise = hashFrac01(seed * 31.7f + 2.3f);
        const float speed = glm::mix(0.85f, 1.10f, speedNoise);
        const float f =
            std::floor(snapshot.timeSec * fps * speed + seed * static_cast<float>(frames));
        int frame = static_cast<int>(std::fmod(f, static_cast<float>(frames)));
        if (frame < 0) frame += frames;

        const int col = frame % cols;
        const int rowFromTop = frame / cols;
        const int row = (rows - 1) - rowFromTop;
        u0 = static_cast<float>(col) / static_cast<float>(cols);
        v0 = static_cast<float>(row) / static_cast<float>(rows);
        u1 = static_cast<float>(col + 1) / static_cast<float>(cols);
        v1 = static_cast<float>(row + 1) / static_cast<float>(rows);
    };

    auto appendBillboardToBatch = [&](WorldIndexedBatch& batch,
                                      const glm::vec4& clip,
                                      const glm::vec3& particlePos,
                                      float pxSize,
                                      float sizeMul,
                                      const glm::vec3& color,
                                      float alpha,
                                      bool secondaryAtlas,
                                      const ParticleSystem::Particle& particle) -> bool {
        if (alpha <= 0.001f || sizeMul <= 0.0001f) return false;
        const float px = std::clamp(pxSize * sizeMul, 3.0f, 160.0f);
        const float halfNdcX = px / std::max(1, ctx.drawableW);
        const float halfNdcY = px / std::max(1, ctx.drawableH);
        if (halfNdcX <= 0.000001f || halfNdcY <= 0.000001f) return false;

        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        glm::vec3 corners[4];
        if (!safeUnprojectClip(ctx.invViewProj,
                               glm::vec4((ndcX - halfNdcX) * clip.w,
                                         (ndcY - halfNdcY) * clip.w,
                                         clip.z,
                                         clip.w),
                               corners[0]) ||
            !safeUnprojectClip(ctx.invViewProj,
                               glm::vec4((ndcX + halfNdcX) * clip.w,
                                         (ndcY - halfNdcY) * clip.w,
                                         clip.z,
                                         clip.w),
                               corners[1]) ||
            !safeUnprojectClip(ctx.invViewProj,
                               glm::vec4((ndcX + halfNdcX) * clip.w,
                                         (ndcY + halfNdcY) * clip.w,
                                         clip.z,
                                         clip.w),
                               corners[2]) ||
            !safeUnprojectClip(ctx.invViewProj,
                               glm::vec4((ndcX - halfNdcX) * clip.w,
                                         (ndcY + halfNdcY) * clip.w,
                                         clip.z,
                                         clip.w),
                               corners[3])) {
            return false;
        }

        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        computeTailFireFrameUv(particle, secondaryAtlas, u0, v0, u1, v1);

        const std::uint32_t baseVertex = static_cast<std::uint32_t>(batch.vertices.size());
        const auto pushVertex = [&](const glm::vec3& p, float u, float v) {
            IRenderBackend::WorldMeshVertex vtx;
            vtx.x = p.x;
            vtx.y = p.y;
            vtx.z = p.z;
            vtx.u = u;
            vtx.v = v;
            vtx.r = color.r;
            vtx.g = color.g;
            vtx.b = color.b;
            vtx.a = alpha;
            batch.vertices.push_back(vtx);
        };

        // Tail-fire atlases are loaded using the legacy ParticleSystem flip policy,
        // and frame-row selection already mirrors legacy sampleAtlas() indexing.
        // Use the normal quad-local UV winding here to avoid a double Y flip.
        pushVertex(corners[0], u0, v0);
        pushVertex(corners[1], u1, v0);
        pushVertex(corners[2], u1, v1);
        pushVertex(corners[3], u0, v1);
        batch.indices.push_back(baseVertex + 0u);
        batch.indices.push_back(baseVertex + 1u);
        batch.indices.push_back(baseVertex + 2u);
        batch.indices.push_back(baseVertex + 0u);
        batch.indices.push_back(baseVertex + 2u);
        batch.indices.push_back(baseVertex + 3u);
        const float distSq = glm::dot(ctx.cameraWorldPos - particlePos, ctx.cameraWorldPos - particlePos);
        batch.sortDepth = std::max(batch.sortDepth, distSq);
        return true;
    };

    bool appendedAny = false;
    for (const auto& particle : snapshot.particles) {
        const float maxLife = std::max(0.0001f, particle.maxLifeSec);
        float age01 = 1.0f - (particle.lifeSec / maxLife);
        age01 = std::clamp(age01, 0.0f, 1.0f);

        const glm::vec4 clip = ctx.viewProj * glm::vec4(particle.pos, 1.0f);
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
            !std::isfinite(clip.z) || !std::isfinite(clip.w)) {
            continue;
        }
        if (clip.w <= 0.0001f) continue;
        const float ndcZ = clip.z / clip.w;
        if (!std::isfinite(ndcZ) || ndcZ < -1.2f || ndcZ > 1.2f) continue;

        const float pxSize = std::clamp(
            particle.sizePx * snapshot.pointScale / std::max(0.0001f, clip.w), 3.0f, 160.0f);

        const float seed = std::clamp(particle.seed, 0.0f, 1.0f);
        const float fade = std::pow(glm::mix(1.0f - age01, 1.0f, 0.25f), 0.75f);
        const float flicker =
            glm::mix(0.92f, 1.08f, hashFrac01(std::floor(snapshot.timeSec * 11.0f) + seed * 91.0f));
        glm::vec3 hybridColor = tailFireRampOrangeRed(age01);
        hybridColor *= (0.95f + 0.15f * flicker);
        hybridColor = glm::clamp(hybridColor, glm::vec3(0.0f), glm::vec3(1.0f));
        const float hybridAlpha = std::clamp(
            (0.74f + 0.30f * (1.0f - age01)) * fade * (0.98f + 0.12f * flicker), 0.0f, 0.95f);
        const glm::vec3 hybridColorPremul = hybridColor * hybridAlpha;

        if (appendBillboardToBatch(
                hybridBatch,
                clip,
                particle.pos,
                pxSize,
                1.12f,
                hybridColorPremul,
                hybridAlpha,
                false,
                particle)) {
            appendedAny = true;
        }

        if (hasSecondary) {
            const float hot = glm::smoothstep(0.10f, 0.55f, 1.0f - age01);
            glm::vec3 coreTint =
                glm::mix(glm::vec3(1.45f, 0.18f, 0.06f),
                         glm::vec3(1.70f, 1.20f, 0.28f),
                         hot);
            coreTint *= 0.90f;
            coreTint *= (0.98f + 0.10f * flicker);
            coreTint = glm::clamp(coreTint, glm::vec3(0.0f), glm::vec3(1.0f));
            const float coreAlpha = std::clamp(
                (0.62f + 0.24f * (1.0f - age01)) * fade * (0.98f + 0.12f * flicker), 0.0f, 0.90f);
            const glm::vec3 coreTintPremul = coreTint * coreAlpha;
            if (appendBillboardToBatch(
                    coreBatch,
                    clip,
                    particle.pos,
                    pxSize,
                    0.86f,
                    coreTintPremul,
                    coreAlpha,
                    true,
                    particle)) {
                appendedAny = true;
            }
        }
    }

    if (!hybridBatch.vertices.empty() && !hybridBatch.indices.empty()) {
        worldIndexedBatches.push_back(std::move(hybridBatch));
    }
    if (hasSecondary && !coreBatch.vertices.empty() && !coreBatch.indices.empty()) {
        worldIndexedBatches.push_back(std::move(coreBatch));
    }
    return appendedAny;
}

} // namespace game::runtime::shared_tail_fire_snapshot_billboards

