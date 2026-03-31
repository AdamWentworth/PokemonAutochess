#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSnapshotBillboards.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "engine/core/Environment.h"
#include "engine/render/IRenderBackend.h"
#include "engine/utils/LogSink.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireExactCpuSnapshotBatches.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSnapshotAtlasCache.h"

namespace game::runtime::shared_tail_fire_snapshot_billboards {
namespace {

using BackendTextureCacheEntry = SharedBackendTextureCacheEntry;
using WorldIndexedBatch = shared_world_batches::WorldIndexedBatch;

engine::log::Sink& tailFireBillboardLog() {
    static engine::log::Sink log("TailFire", &std::cout, &std::cerr);
    return log;
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

bool computeBillboardAxes(const glm::mat4& invViewProj,
                          const glm::vec3& centerWorld,
                          const glm::vec4& clip,
                          float halfNdcX,
                          float halfNdcY,
                          glm::vec3& outRight,
                          glm::vec3& outUp) {
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    glm::vec3 rightWorld;
    glm::vec3 upWorld;
    if (!safeUnprojectClip(
            invViewProj,
            glm::vec4((ndcX + halfNdcX) * clip.w, ndcY * clip.w, clip.z, clip.w),
            rightWorld) ||
        !safeUnprojectClip(
            invViewProj,
            glm::vec4(ndcX * clip.w, (ndcY + halfNdcY) * clip.w, clip.z, clip.w),
            upWorld)) {
        return false;
    }

    outRight = rightWorld - centerWorld;
    outUp = upWorld - centerWorld;
    return std::isfinite(outRight.x) && std::isfinite(outRight.y) &&
           std::isfinite(outRight.z) && std::isfinite(outUp.x) &&
           std::isfinite(outUp.y) && std::isfinite(outUp.z);
}

glm::vec3 safeNormOr(glm::vec3 v, const glm::vec3& fallback) {
    const float len2 = glm::dot(v, v);
    if (len2 <= 1e-10f) return fallback;
    return v * (1.0f / std::sqrt(len2));
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

const ParticleSystem::Particle* pickRepresentativeParticle(
    const ParticleSystem::RenderSnapshot& snapshot) {
    const ParticleSystem::Particle* representative = nullptr;
    float bestScore = -1.0f;
    for (const auto& particle : snapshot.particles) {
        const float maxLife = std::max(0.0001f, particle.maxLifeSec);
        const float remaining01 = std::clamp(particle.lifeSec / maxLife, 0.0f, 1.0f);
        const float score = remaining01 * 1000.0f + particle.sizePx;
        if (!representative || score > bestScore) {
            representative = &particle;
            bestScore = score;
        }
    }
    return representative;
}

bool hasValidTailFireAnchor(const AppendContext& ctx) {
    if (!ctx.tailFireAnchors) return false;
    for (const auto& [unitId, anchor] : *ctx.tailFireAnchors) {
        (void)unitId;
        if (anchor.valid && !anchor.meshCarrierActive) return true;
    }
    return false;
}

bool tailFireDebugEnvEnabled() {
    static const bool enabled = engine::env::flagEnabled("PAC_TAIL_FIRE_DEBUG");
    return enabled;
}

bool tailFireDebugShouldLogBillboard(const AppendContext& ctx, int unitId) {
    if (!(ctx.tailFireDebugEnabled || tailFireDebugEnvEnabled())) return false;
    static std::unordered_map<int, std::chrono::steady_clock::time_point> sLastLogByUnit;
    const auto now = std::chrono::steady_clock::now();
    auto it = sLastLogByUnit.find(unitId);
    if (it != sLastLogByUnit.end() &&
        (now - it->second) < std::chrono::milliseconds(750)) {
        return false;
    }
    sLastLogByUnit[unitId] = now;
    return true;
}

} // namespace

bool appendTailFireSnapshotBillboards(
    const char* label,
    const ParticleSystem::RenderSnapshot& snapshot,
    std::uint8_t blendMode,
    const AppendContext& ctx,
    std::vector<WorldIndexedBatch>& worldIndexedBatches) {
    if (!label) return false;
    const bool anchoredSingleFlipbook =
        !snapshot.useSecondaryFlipbook && hasValidTailFireAnchor(ctx);
    if (snapshot.particles.empty() && !anchoredSingleFlipbook) return false;

    const bool useExactTailFirePath = snapshot.useSecondaryFlipbook;
    if (useExactTailFirePath &&
        appendTailFireExactGpuBatch(label, snapshot, blendMode, ctx, worldIndexedBatches)) {
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

    if (useExactTailFirePath &&
        ctx.tailFireExactCpuEnabled &&
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
        const bool coherentSingleFlipbook = !snapshot.useSecondaryFlipbook;
        const float speedNoise = hashFrac01(seed * 31.7f + 2.3f);
        const float speed = coherentSingleFlipbook ? 1.0f : glm::mix(0.85f, 1.10f, speedNoise);
        const float phase = coherentSingleFlipbook ? 0.0f : (seed * static_cast<float>(frames));
        const float f =
            std::floor(snapshot.timeSec * fps * speed + phase);
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
                                      const ParticleSystem::Particle& particle,
                                      bool flipV = false,
                                      bool anchorBottom = false) -> bool {
        if (alpha <= 0.001f || sizeMul <= 0.0001f) return false;
        const float px = std::clamp(pxSize * sizeMul, 3.0f, 160.0f);
        const float halfNdcX = px / std::max(1, ctx.drawableW);
        const float halfNdcY = px / std::max(1, ctx.drawableH);
        if (halfNdcX <= 0.000001f || halfNdcY <= 0.000001f) return false;

        glm::vec3 rightAxis;
        glm::vec3 upAxis;
        if (!computeBillboardAxes(
                ctx.invViewProj, particlePos, clip, halfNdcX, halfNdcY, rightAxis, upAxis)) {
            return false;
        }
        const glm::vec3 bottomCenter = particlePos;
        const glm::vec3 center = anchorBottom ? (bottomCenter + upAxis) : bottomCenter;
        const glm::vec3 corners[4] = {
            center - rightAxis - upAxis,
            center + rightAxis - upAxis,
            center + rightAxis + upAxis,
            center - rightAxis + upAxis,
        };

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

        const float topV = flipV ? v1 : v0;
        const float bottomV = flipV ? v0 : v1;
        pushVertex(corners[0], u0, topV);
        pushVertex(corners[1], u1, topV);
        pushVertex(corners[2], u1, bottomV);
        pushVertex(corners[3], u0, bottomV);
        batch.indices.push_back(baseVertex + 0u);
        batch.indices.push_back(baseVertex + 1u);
        batch.indices.push_back(baseVertex + 2u);
        batch.indices.push_back(baseVertex + 0u);
        batch.indices.push_back(baseVertex + 2u);
        batch.indices.push_back(baseVertex + 3u);
        const float distSq = glm::dot(ctx.cameraWorldPos - center, ctx.cameraWorldPos - center);
        batch.sortDepth = std::max(batch.sortDepth, distSq);
        return true;
    };

    auto appendAxisBillboardToBatch = [&](WorldIndexedBatch& batch,
                                          const glm::vec3& basePos,
                                          const glm::vec3& tipPos,
                                          float pxSize,
                                          float sizeMul,
                                          const glm::vec3& color,
                                          float alpha,
                                          bool secondaryAtlas,
                                          const ParticleSystem::Particle& particle,
                                          float cameraPullWorld) -> bool {
        if (alpha <= 0.001f || sizeMul <= 0.0001f) return false;
        const glm::vec3 fireAxis = tipPos - basePos;
        const float fireAxisLen = glm::length(fireAxis);
        if (fireAxisLen <= 1e-5f) return false;

        const glm::vec3 center = (basePos + tipPos) * 0.5f;
        const glm::vec4 centerClip = ctx.viewProj * glm::vec4(center, 1.0f);
        if (!std::isfinite(centerClip.x) || !std::isfinite(centerClip.y) ||
            !std::isfinite(centerClip.z) || !std::isfinite(centerClip.w) ||
            centerClip.w <= 0.0001f) {
            return false;
        }

        const float px = std::clamp(pxSize * sizeMul, 3.0f, 160.0f);
        const float halfNdcX = px / std::max(1, ctx.drawableW);
        const float halfNdcY = px / std::max(1, ctx.drawableH);
        if (halfNdcX <= 0.000001f || halfNdcY <= 0.000001f) return false;

        glm::vec3 rawRightAxis;
        glm::vec3 rawUpAxis;
        if (!computeBillboardAxes(
                ctx.invViewProj, center, centerClip, halfNdcX, halfNdcY, rawRightAxis, rawUpAxis)) {
            return false;
        }

        const glm::vec3 fireDir = fireAxis * (1.0f / fireAxisLen);
        glm::vec3 widthDir = rawRightAxis - fireDir * glm::dot(rawRightAxis, fireDir);
        if (glm::dot(widthDir, widthDir) <= 1e-10f) {
            widthDir = rawUpAxis - fireDir * glm::dot(rawUpAxis, fireDir);
        }
        if (glm::dot(widthDir, widthDir) <= 1e-10f) {
            widthDir = glm::cross(safeNormOr(ctx.cameraWorldPos - center, glm::vec3(0.0f, 0.0f, 1.0f)), fireDir);
        }
        widthDir = safeNormOr(widthDir, glm::vec3(1.0f, 0.0f, 0.0f));

        const float halfWidth = std::max(glm::length(rawRightAxis), glm::length(rawUpAxis)) * 1.16f;
        if (!std::isfinite(halfWidth) || halfWidth <= 1e-6f) return false;

        const glm::vec3 toCamera =
            safeNormOr(ctx.cameraWorldPos - center, -safeNormOr(rawUpAxis, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 overlapOffset = toCamera * cameraPullWorld;

        const glm::vec3 pulledBase = basePos + overlapOffset;
        const glm::vec3 pulledTip = tipPos + overlapOffset;
        const glm::vec3 widthAxis = widthDir * halfWidth;
        const glm::vec3 corners[4] = {
            pulledBase - widthAxis,
            pulledBase + widthAxis,
            pulledTip + widthAxis,
            pulledTip - widthAxis,
        };

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
        const glm::vec3 sortCenter = (pulledBase + pulledTip) * 0.5f;
        const float distSq = glm::dot(ctx.cameraWorldPos - sortCenter, ctx.cameraWorldPos - sortCenter);
        batch.sortDepth = std::max(batch.sortDepth, distSq);
        return true;
    };

    if (!snapshot.useSecondaryFlipbook) {
        const ParticleSystem::Particle* representative = pickRepresentativeParticle(snapshot);
        ParticleSystem::Particle frameParticle{};
        if (representative) {
            frameParticle = *representative;
        } else {
            frameParticle.lifeSec = 1.0f;
            frameParticle.maxLifeSec = 1.0f;
            frameParticle.sizePx = 0.30f;
            frameParticle.seed = 0.0f;
        }

        bool appendedAnchored = false;
        if (ctx.tailFireAnchors) {
            for (const auto& [unitId, anchor] : *ctx.tailFireAnchors) {
                (void)unitId;
                if (!anchor.valid || anchor.meshCarrierActive) continue;

                const float scale = std::max(1.0f, anchor.particleSizeScale);
                const glm::vec3 localUp =
                    safeNormOr(anchor.basis * glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                const glm::vec3 localBack =
                    safeNormOr(anchor.backDir, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 renderBasePos = anchor.pos;
                glm::vec3 renderTipPos = anchor.tipPos;
                float anchoredSizeMul = 1.0f;
                if (anchor.exactFireAnchor) {
                    const glm::vec3 fireUp = safeNormOr(anchor.tipPos - anchor.pos, localUp);
                    const float fireSpan = glm::length(anchor.tipPos - anchor.pos);
                    if (fireSpan > 1e-5f) {
                        // The Charmander flipbook frames carry ~10% transparent padding at
                        // both the bottom and the top. Expand the billboard span so the
                        // visible fire, not the padded frame bounds, matches the authored
                        // base/tip helpers from the GLB.
                        constexpr float kBottomPadRatio = 0.099f;
                        constexpr float kTopPadRatio = 0.101f;
                        constexpr float kBaseEngulfRatio = 0.070f;
                        constexpr float kTipEngulfRatio = 0.050f;
                        constexpr float kVisibleHeightRatio =
                            1.0f - kBottomPadRatio - kTopPadRatio;
                        renderBasePos =
                            anchor.pos - fireUp * (
                                fireSpan * (kBottomPadRatio / kVisibleHeightRatio) +
                                fireSpan * kBaseEngulfRatio);
                        renderTipPos =
                            anchor.tipPos + fireUp * (
                                fireSpan * (kTopPadRatio / kVisibleHeightRatio) +
                                fireSpan * kTipEngulfRatio);
                        anchoredSizeMul = 1.18f;
                    }
                }

                const glm::vec4 clip = ctx.viewProj * glm::vec4(renderBasePos, 1.0f);
                if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
                    !std::isfinite(clip.z) || !std::isfinite(clip.w) ||
                    clip.w <= 0.0001f) {
                    continue;
                }
                const float ndcZ = clip.z / clip.w;
                if (!std::isfinite(ndcZ) || ndcZ < -1.2f || ndcZ > 1.2f) {
                    continue;
                }

                const glm::vec3 toCamera =
                    safeNormOr(ctx.cameraWorldPos - renderBasePos, -localBack);
                const float engulfLift = anchor.exactFireAnchor ? 0.0f : (0.022f * scale);
                const float cameraPull = anchor.exactFireAnchor ? (0.020f * scale) : (0.007f * scale);
                const glm::vec3 engulfOffset =
                    localUp * engulfLift +
                    toCamera * cameraPull;

                float pxSize = std::clamp(
                    (0.34f * scale) * snapshot.pointScale / std::max(0.0001f, clip.w),
                    3.0f,
                    160.0f);
                if (anchor.exactFireAnchor) {
                    const glm::vec4 tipClip = ctx.viewProj * glm::vec4(renderTipPos, 1.0f);
                    if (std::isfinite(tipClip.x) && std::isfinite(tipClip.y) &&
                        std::isfinite(tipClip.z) && std::isfinite(tipClip.w) &&
                        tipClip.w > 0.0001f) {
                        const float baseNdcY = clip.y / clip.w;
                        const float tipNdcY = tipClip.y / tipClip.w;
                        const float spanPx =
                            std::abs(tipNdcY - baseNdcY) * (static_cast<float>(ctx.drawableH) * 0.5f);
                        if (std::isfinite(spanPx)) {
                            pxSize = std::clamp(spanPx, 3.0f, 160.0f);
                        }
                    }
                }
                if (tailFireDebugShouldLogBillboard(ctx, unitId)) {
                    std::ostringstream msg;
                    msg << "[TailFire][Debug][Billboard] unit=" << unitId
                        << " exact=" << (anchor.exactFireAnchor ? 1 : 0)
                        << " anchorBase=(" << anchor.pos.x << "," << anchor.pos.y << "," << anchor.pos.z << ")"
                        << " anchorTip=(" << anchor.tipPos.x << "," << anchor.tipPos.y << "," << anchor.tipPos.z << ")"
                        << " renderBase=(" << renderBasePos.x << "," << renderBasePos.y << "," << renderBasePos.z << ")"
                        << " renderTip=(" << renderTipPos.x << "," << renderTipPos.y << "," << renderTipPos.z << ")"
                        << " finalBase=(" << (renderBasePos + engulfOffset).x << "," << (renderBasePos + engulfOffset).y
                        << "," << (renderBasePos + engulfOffset).z << ")"
                        << " clipW=" << clip.w
                        << " pxSize=" << pxSize
                        << " scale=" << scale
                        << " cameraPull=" << cameraPull
                        << " engulfLift=" << engulfLift;
                    tailFireBillboardLog().info(msg.str());
                }
                const float alpha = 0.98f;
                const glm::vec3 premulColor(alpha, alpha, alpha);
                const bool appended = anchor.exactFireAnchor
                    ? appendAxisBillboardToBatch(
                        hybridBatch,
                        renderBasePos,
                        renderTipPos,
                        pxSize,
                        anchoredSizeMul,
                        premulColor,
                        alpha,
                        false,
                        frameParticle,
                        cameraPull)
                    : appendBillboardToBatch(
                        hybridBatch,
                        clip,
                        renderBasePos + engulfOffset,
                        pxSize,
                        anchoredSizeMul,
                        premulColor,
                        alpha,
                        false,
                        frameParticle,
                        false,
                        true);
                if (appended) appendedAnchored = true;
            }
        }

        if (appendedAnchored && !hybridBatch.vertices.empty() && !hybridBatch.indices.empty()) {
            worldIndexedBatches.push_back(std::move(hybridBatch));
            return true;
        }

        if (!representative) {
            return false;
        }

        const auto& particle = *representative;
        const float maxLife = std::max(0.0001f, particle.maxLifeSec);
        float age01 = 1.0f - (particle.lifeSec / maxLife);
        age01 = std::clamp(age01, 0.0f, 1.0f);

        const glm::vec4 clip = ctx.viewProj * glm::vec4(particle.pos, 1.0f);
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
            !std::isfinite(clip.z) || !std::isfinite(clip.w) ||
            clip.w <= 0.0001f) {
            return false;
        }
        const float ndcZ = clip.z / clip.w;
        if (!std::isfinite(ndcZ) || ndcZ < -1.2f || ndcZ > 1.2f) {
            return false;
        }

        const float pxSize = std::clamp(
            particle.sizePx * snapshot.pointScale / std::max(0.0001f, clip.w), 3.0f, 160.0f);
        const glm::vec3 hybridColor = tailFireRampOrangeRed(age01);
        const float hybridAlpha = 0.92f;
        const glm::vec3 hybridColorPremul = hybridColor * hybridAlpha;
        const bool appended = appendBillboardToBatch(
            hybridBatch,
            clip,
            particle.pos,
            pxSize,
            1.18f,
            hybridColorPremul,
            hybridAlpha,
            false,
            particle,
            false,
            true);
        if (appended && !hybridBatch.vertices.empty() && !hybridBatch.indices.empty()) {
            worldIndexedBatches.push_back(std::move(hybridBatch));
        }
        return appended;
    }

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
