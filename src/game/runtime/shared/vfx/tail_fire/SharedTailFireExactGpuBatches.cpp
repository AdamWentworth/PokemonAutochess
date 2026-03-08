#include "game/runtime/shared/vfx/tail_fire/SharedTailFireExactGpuBatches.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

bool safeUnprojectClip(const glm::mat4& invViewProj, const glm::vec4& clipPos, glm::vec3& outWorld) {
    const glm::vec4 world = invViewProj * clipPos;
    if (!std::isfinite(world.x) || !std::isfinite(world.y) ||
        !std::isfinite(world.z) || !std::isfinite(world.w)) {
        return false;
    }
    if (std::fabs(world.w) <= 0.000001f) return false;
    outWorld = glm::vec3(world) / world.w;
    return std::isfinite(outWorld.x) && std::isfinite(outWorld.y) && std::isfinite(outWorld.z);
}

} // namespace

namespace game::runtime::shared_tail_fire_exact_gpu {

bool appendBatch(const char* label,
                 const ParticleSystem::RenderSnapshot& snapshot,
                 const BuildContext& ctx,
                 const AtlasView& atlas,
                 std::vector<shared_world_batches::WorldIndexedBatch>& outBatches) {
    if (!label || snapshot.particles.empty()) return false;
    if (!snapshot.useFlipbook || snapshot.flipbookPath.empty()) return false;
    if (!atlas.rgba || atlas.width <= 0 || atlas.height <= 0) return false;

    using shared_world_batches::WorldIndexedBatch;
    WorldIndexedBatch batch;
    // Reuse the atlas cache key directly so startup prewarm and runtime exact-fire batches
    // hit the same backend world-texture cache entry instead of uploading the atlas twice.
    batch.textureKey = atlas.cacheKey;
    batch.textureRgba = atlas.rgba;
    batch.textureWidth = atlas.width;
    batch.textureHeight = atlas.height;
    batch.textureWrapS = 33071; // clamp
    batch.textureWrapT = 33071; // clamp
    batch.alphaMode = 2u;
    batch.blendMode = ctx.blendMode;
    batch.materialMode = 1u; // exact fire_tail in backend shader
    batch.alphaCutoff = 0.0f;
    batch.sortDepth = 0.0f;
    batch.materialTimeSec = snapshot.timeSec;
    batch.materialFlags = 1.0f + (atlas.hasSecondary ? 2.0f : 0.0f);
    batch.materialAtlasWidth = static_cast<float>(batch.textureWidth);
    batch.materialAtlasHeight = static_cast<float>(batch.textureHeight);
    batch.materialRect0U = atlas.rect0.x;
    batch.materialRect0V = atlas.rect0.y;
    batch.materialRect0W = atlas.rect0.z;
    batch.materialRect0H = atlas.rect0.w;
    batch.materialRect1U = atlas.rect1.x;
    batch.materialRect1V = atlas.rect1.y;
    batch.materialRect1W = atlas.rect1.z;
    batch.materialRect1H = atlas.rect1.w;
    batch.materialFlipbook0Cols = static_cast<float>(std::max(1, snapshot.flipbookCols));
    batch.materialFlipbook0Rows = static_cast<float>(std::max(1, snapshot.flipbookRows));
    batch.materialFlipbook0Frames = static_cast<float>(std::max(1, snapshot.flipbookFrames));
    batch.materialFlipbook0Fps = std::max(0.0f, snapshot.flipbookFps);
    batch.materialFlipbook1Cols = static_cast<float>(std::max(1, snapshot.flipbookCols2));
    batch.materialFlipbook1Rows = static_cast<float>(std::max(1, snapshot.flipbookRows2));
    batch.materialFlipbook1Frames = static_cast<float>(std::max(1, snapshot.flipbookFrames2));
    batch.materialFlipbook1Fps = std::max(0.0f, snapshot.flipbookFps2);
    batch.vertices.reserve(snapshot.particles.size() * 4u);
    batch.indices.reserve(snapshot.particles.size() * 6u);

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
            particle.sizePx * snapshot.pointScale / std::max(0.0001f, clip.w),
            3.0f,
            160.0f);
        const float halfNdcX = pxSize / std::max(1, ctx.drawableW);
        const float halfNdcY = pxSize / std::max(1, ctx.drawableH);
        if (halfNdcX <= 0.000001f || halfNdcY <= 0.000001f) continue;

        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        glm::vec3 corners[4];
        if (!safeUnprojectClip(
                ctx.invViewProj,
                glm::vec4((ndcX - halfNdcX) * clip.w, (ndcY - halfNdcY) * clip.w, clip.z, clip.w),
                corners[0]) ||
            !safeUnprojectClip(
                ctx.invViewProj,
                glm::vec4((ndcX + halfNdcX) * clip.w, (ndcY - halfNdcY) * clip.w, clip.z, clip.w),
                corners[1]) ||
            !safeUnprojectClip(
                ctx.invViewProj,
                glm::vec4((ndcX + halfNdcX) * clip.w, (ndcY + halfNdcY) * clip.w, clip.z, clip.w),
                corners[2]) ||
            !safeUnprojectClip(
                ctx.invViewProj,
                glm::vec4((ndcX - halfNdcX) * clip.w, (ndcY + halfNdcY) * clip.w, clip.z, clip.w),
                corners[3])) {
            continue;
        }

        const std::uint32_t baseVertex = static_cast<std::uint32_t>(batch.vertices.size());
        const float seed = std::clamp(particle.seed, 0.0f, 1.0f);
        const auto pushVertex = [&](const glm::vec3& p, float u, float v) {
            IRenderBackend::WorldMeshVertex vtx;
            vtx.x = p.x;
            vtx.y = p.y;
            vtx.z = p.z;
            vtx.u = u;
            vtx.v = v;
            vtx.r = age01;
            vtx.g = seed;
            vtx.b = 1.0f;
            vtx.a = 1.0f;
            batch.vertices.push_back(vtx);
        };
        pushVertex(corners[0], 0.0f, 0.0f);
        pushVertex(corners[1], 1.0f, 0.0f);
        pushVertex(corners[2], 1.0f, 1.0f);
        pushVertex(corners[3], 0.0f, 1.0f);
        batch.indices.push_back(baseVertex + 0u);
        batch.indices.push_back(baseVertex + 1u);
        batch.indices.push_back(baseVertex + 2u);
        batch.indices.push_back(baseVertex + 0u);
        batch.indices.push_back(baseVertex + 2u);
        batch.indices.push_back(baseVertex + 3u);
        const float distSq =
            glm::dot(ctx.cameraWorldPos - particle.pos, ctx.cameraWorldPos - particle.pos);
        batch.sortDepth = std::max(batch.sortDepth, distSq);
        appendedAny = true;
    }

    if (appendedAny && !batch.vertices.empty() && !batch.indices.empty()) {
        outBatches.push_back(std::move(batch));
        return true;
    }
    return false;
}

} // namespace game::runtime::shared_tail_fire_exact_gpu

