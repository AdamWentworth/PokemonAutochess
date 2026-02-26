#include "game/runtime/shared/SharedParticleBillboardBatches.h"

#include <algorithm>
#include <cmath>

#include "game/runtime/shared/SharedParticleVfxStyles.h"

namespace game::runtime::shared_particle_billboards {

namespace {

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

} // namespace

bool appendGenericBatch(const ParticleSystem::RenderSnapshot& snapshot,
                        const BuildContext& ctx,
                        game::runtime::shared_world_batches::WorldIndexedBatch batchTemplate,
                        std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>& outBatches) {
    using game::runtime::shared_particle_vfx_styles::resolveStyle;
    using game::runtime::shared_world_batches::WorldIndexedBatch;

    if (snapshot.particles.empty()) return false;
    WorldIndexedBatch batch = std::move(batchTemplate);
    batch.vertices.reserve(snapshot.particles.size() * 4u);
    batch.indices.reserve(snapshot.particles.size() * 6u);

    const int cols = std::max(1, snapshot.flipbookCols);
    const int rows = std::max(1, snapshot.flipbookRows);
    const int maxFrames = std::max(1, cols * rows);
    const int frames = std::clamp(snapshot.flipbookFrames, 1, maxFrames);
    bool appendedAny = false;

    for (const auto& particle : snapshot.particles) {
        const float maxLife = std::max(0.0001f, particle.maxLifeSec);
        float age01 = 1.0f - (particle.lifeSec / maxLife);
        age01 = std::clamp(age01, 0.0f, 1.0f);

        const auto style = resolveStyle(snapshot, particle, age01);
        if (style.alpha <= 0.001f) continue;

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

        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
        if (snapshot.useFlipbook && frames > 1 && cols > 0 && rows > 0) {
            int frame = static_cast<int>(std::round(age01 * static_cast<float>(frames - 1)));
            frame = std::clamp(frame, 0, frames - 1);
            const int col = frame % cols;
            const int row = frame / cols;
            u0 = static_cast<float>(col) / static_cast<float>(cols);
            v0 = static_cast<float>(row) / static_cast<float>(rows);
            u1 = static_cast<float>(col + 1) / static_cast<float>(cols);
            v1 = static_cast<float>(row + 1) / static_cast<float>(rows);
        }

        const std::uint32_t baseVertex = static_cast<std::uint32_t>(batch.vertices.size());
        const auto pushVertex = [&](const glm::vec3& p, float u, float v) {
            IRenderBackend::WorldMeshVertex vtx;
            vtx.x = p.x;
            vtx.y = p.y;
            vtx.z = p.z;
            vtx.u = u;
            vtx.v = v;
            vtx.r = style.color.r;
            vtx.g = style.color.g;
            vtx.b = style.color.b;
            vtx.a = style.alpha;
            batch.vertices.push_back(vtx);
        };
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

} // namespace game::runtime::shared_particle_billboards
