#include "game/runtime/SharedParticleSnapshotBillboards.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/SharedParticleBillboardBatches.h"
#include "game/runtime/SharedTailFireAtlasHelpers.h"
#include "game/runtime/SharedTailFireExactGpuBatches.h"

namespace game::runtime::shared_particle_snapshot_billboards {
namespace {

std::string toLowerCopyLocal(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

} // namespace

bool appendSnapshotAsBillboards(
    const char* label,
    const ParticleSystem::RenderSnapshot& snapshot,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureTextureFn,
    bool tailFireExactCpuEnabledFlag,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches) {
    using BackendTextureCacheEntry = SharedBackendTextureCacheEntry;
    using WorldIndexedBatch = shared_world_batches::WorldIndexedBatch;

    auto ensureBackendTextureLoaded =
        [&](const std::string& texturePath, bool flipVertical = false) -> BackendTextureCacheEntry* {
            if (!ensureTextureFn) return nullptr;
            return ensureTextureFn(texturePath, flipVertical);
        };
    auto backendUseExactTailFireCpuPathEnabled = [&]() { return tailFireExactCpuEnabledFlag; };
    auto toLowerCopy = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    };
                    const auto toBackendBlendMode =
                        [](ParticleSystem::BlendMode mode) -> std::uint8_t {
                            switch (mode) {
                            case ParticleSystem::BlendMode::Additive:
                                return 1u;
                            case ParticleSystem::BlendMode::Premultiplied:
                                return 2u;
                            case ParticleSystem::BlendMode::Alpha:
                            default:
                                return 0u;
                            }
                        };

                    const auto safeUnprojectClip =
                        [&](const glm::vec4& clipPos, glm::vec3& outWorld) -> bool {
                            const glm::vec4 world = invViewProj * clipPos;
                            if (!std::isfinite(world.x) || !std::isfinite(world.y) ||
                                !std::isfinite(world.z) || !std::isfinite(world.w)) {
                                return false;
                            }
                            if (std::fabs(world.w) <= 0.000001f) return false;
                            outWorld = glm::vec3(world) / world.w;
                            return std::isfinite(outWorld.x) && std::isfinite(outWorld.y) &&
                                   std::isfinite(outWorld.z);
                        };

                    const auto hashFrac01 = [](float x) {
                        const float s = std::sin(x * 12.9898f) * 43758.5453f;
                        return s - std::floor(s);
                    };
                    const auto tailFireRampOrangeRed = [](float age01) {
                        const glm::vec3 hot(1.05f, 0.42f, 0.18f);
                        const glm::vec3 mid(0.90f, 0.30f, 0.14f);
                        const glm::vec3 cool(0.58f, 0.16f, 0.10f);
                        glm::vec3 c = glm::mix(hot, mid, glm::smoothstep(0.0f, 0.65f, age01));
                        c = glm::mix(c, cool, glm::smoothstep(0.55f, 1.0f, age01));
                        c *= 0.70f;
                        const float l = glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
                        c = glm::mix(glm::vec3(l), c, 0.95f);
                        return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
                    };
                    const auto resolveTailFirePremulAtlas =
                        [&](const std::string& atlasPath) -> BackendTextureCacheEntry* {
                            if (atlasPath.empty()) return nullptr;
                            // Legacy ParticleSystem loads VFX flipbooks with stb vertical flip enabled.
                            // Match that texture orientation here so the shared fire_tail UV logic aligns.
                            BackendTextureCacheEntry* src = ensureBackendTextureLoaded(atlasPath, true);
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
                            if (!game::runtime::shared_tail_fire_atlas::buildPremultipliedAtlas(
                                    srcView, premul)) {
                                return nullptr;
                            }
                            baked.width = premul.width;
                            baked.height = premul.height;
                            baked.rgba = std::move(premul.rgba);
                            baked.valid = (baked.width > 0 && baked.height > 0 && !baked.rgba.empty());
                            return &baked;
                        };

                    struct TailFireCombinedAtlasInfo {
                        BackendTextureCacheEntry* atlas = nullptr;
                        std::string cacheKey;
                        glm::vec4 rect0{0.0f, 0.0f, 1.0f, 1.0f};
                        glm::vec4 rect1{0.0f, 0.0f, 1.0f, 1.0f};
                        bool hasSecondary = false;
                    };

                    const auto resolveTailFireCombinedAtlas =
                        [&](const ParticleSystem::RenderSnapshot& snapshot) -> TailFireCombinedAtlasInfo {
                            TailFireCombinedAtlasInfo out;
                            if (!snapshot.useFlipbook || snapshot.flipbookPath.empty()) return out;
                            BackendTextureCacheEntry* primaryRaw = ensureBackendTextureLoaded(snapshot.flipbookPath, true);
                            if (!primaryRaw || !primaryRaw->valid || primaryRaw->rgba.empty() ||
                                primaryRaw->width <= 0 || primaryRaw->height <= 0) {
                                return out;
                            }
                            BackendTextureCacheEntry* secondaryRaw = nullptr;
                            if (snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty()) {
                                secondaryRaw = ensureBackendTextureLoaded(snapshot.flipbookPath2, true);
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
                                const game::runtime::shared_tail_fire_atlas::RgbaTextureView* secondaryViewPtr =
                                    nullptr;
                                if (out.hasSecondary) {
                                    secondaryView = {secondaryRaw->rgba.data(),
                                                     secondaryRaw->width,
                                                     secondaryRaw->height};
                                    secondaryViewPtr = &secondaryView;
                                }
                                if (game::runtime::shared_tail_fire_atlas::buildCombinedAtlas(
                                        primaryView, secondaryViewPtr, builtAtlas, builtInfo)) {
                                    combined.width = builtAtlas.width;
                                    combined.height = builtAtlas.height;
                                    combined.rgba = std::move(builtAtlas.rgba);
                                    combined.valid = (combined.width > 0 && combined.height > 0 &&
                                                      !combined.rgba.empty());
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
                                // Cache hit path: reconstruct atlas rects from source sizes.
                                const float invW =
                                    1.0f / static_cast<float>(std::max(1, combined.width));
                                const float invH =
                                    1.0f / static_cast<float>(std::max(1, combined.height));
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
                        };

                    const auto appendTailFireExactGpuBatch =
                        [&](const char* label,
                            const ParticleSystem::RenderSnapshot& snapshot,
                            std::uint8_t blendMode) -> bool {
                            if (!label || snapshot.particles.empty()) return false;
                            if (!snapshot.useFlipbook || snapshot.flipbookPath.empty()) return false;

                            TailFireCombinedAtlasInfo atlasInfo = resolveTailFireCombinedAtlas(snapshot);
                            if (!atlasInfo.atlas || !atlasInfo.atlas->valid || atlasInfo.atlas->rgba.empty()) {
                                return false;
                            }
                            game::runtime::shared_tail_fire_exact_gpu::BuildContext tailCtx;
                            tailCtx.viewProj = viewProj;
                            tailCtx.invViewProj = invViewProj;
                            tailCtx.cameraWorldPos = cameraWorldPos;
                            tailCtx.drawableW = drawableW;
                            tailCtx.drawableH = drawableH;
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
                        };

                    const auto appendSnapshotAsBillboards =
                        [&](const char* label, const ParticleSystem::RenderSnapshot& snapshot) -> bool {
                            if (!label) return false;
                            if (snapshot.particles.empty()) return false;

                            const std::uint8_t blendMode = toBackendBlendMode(snapshot.renderSettings.blend);
                            const std::string frag = toLowerCopy(snapshot.shaderFragPath);
                            const bool tailFireShader = (frag.find("fire_tail") != std::string::npos);
                            const bool tailFireExactCpuEnabled = backendUseExactTailFireCpuPathEnabled();

                            if (tailFireShader && appendTailFireExactGpuBatch(label, snapshot, blendMode)) {
                                return true;
                            }

                            if (tailFireShader && snapshot.useFlipbook && !snapshot.flipbookPath.empty()) {
                                BackendTextureCacheEntry* primaryTex = resolveTailFirePremulAtlas(snapshot.flipbookPath);
                                BackendTextureCacheEntry* secondaryTex =
                                    (snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty())
                                        ? resolveTailFirePremulAtlas(snapshot.flipbookPath2)
                                        : nullptr;
                                if (!primaryTex || !primaryTex->valid || primaryTex->rgba.empty()) {
                                    return false;
                                }
                                BackendTextureCacheEntry* primaryRawTex =
                                    ensureBackendTextureLoaded(snapshot.flipbookPath, true);
                                BackendTextureCacheEntry* secondaryRawTex =
                                    (snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty())
                                        ? ensureBackendTextureLoaded(snapshot.flipbookPath2, true)
                                        : nullptr;

                                auto rawAtlasValid = [](const BackendTextureCacheEntry* t) {
                                    return t && t->valid && !t->rgba.empty() && t->width > 0 && t->height > 0;
                                };

                                if (tailFireExactCpuEnabled && rawAtlasValid(primaryRawTex)) {
                                    WorldIndexedBatch exactBatch;
                                    exactBatch.textureKey =
                                        std::string("particle:") + label + ":tail_fire_exact_cpu";
                                    exactBatch.textureWrapS = 33071;
                                    exactBatch.textureWrapT = 33071;
                                    exactBatch.alphaMode = 2u;
                                    exactBatch.blendMode = toBackendBlendMode(snapshot.renderSettings.blend);
                                    exactBatch.alphaCutoff = 0.0f;
                                    exactBatch.sortDepth = 0.0f;
                                    exactBatch.vertices.reserve(snapshot.particles.size() * 4u);
                                    exactBatch.indices.reserve(snapshot.particles.size() * 6u);

                                    // CPU port of fire_tail.frag is expensive; use cached quantized tiles so
                                    // shared paths stay inspectable while preserving the legacy shader look.
                                    const int tileSize = 24;
                                    const int tilePad = 1;
                                    const int tilePitch = tileSize + tilePad * 2;
                                    const int particleCount = static_cast<int>(snapshot.particles.size());
                                    const int atlasCols = std::max(1, static_cast<int>(std::ceil(std::sqrt(
                                        static_cast<float>(std::max(1, particleCount))))));
                                    const int atlasRows = std::max(1, (particleCount + atlasCols - 1) / atlasCols);
                                    const int atlasW = atlasCols * tilePitch;
                                    const int atlasH = atlasRows * tilePitch;
                                    exactBatch.textureWidth = atlasW;
                                    exactBatch.textureHeight = atlasH;
                                    exactBatch.ownedTextureRgba.assign(
                                        static_cast<std::size_t>(atlasW) * static_cast<std::size_t>(atlasH) * 4u, 0u);
                                    exactBatch.textureRgba = exactBatch.ownedTextureRgba.data();

                                    const auto clamp01 = [](float x) { return std::clamp(x, 0.0f, 1.0f); };
                                    const auto fractf = [](float x) { return x - std::floor(x); };
                                    const auto hash11 = [&](float x) {
                                        return fractf(std::sin(x * 12.9898f) * 43758.5453f);
                                    };
                                    const auto hash21 = [&](const glm::vec2& p) {
                                        const float n = glm::dot(p, glm::vec2(127.1f, 311.7f));
                                        return fractf(std::sin(n) * 43758.5453f);
                                    };
                                    const auto smoothstepf = [](float e0, float e1, float x) {
                                        const float d = e1 - e0;
                                        if (std::fabs(d) <= 1e-6f) {
                                            return (x < e0) ? 0.0f : 1.0f;
                                        }
                                        const float t = std::clamp((x - e0) / d, 0.0f, 1.0f);
                                        return t * t * (3.0f - 2.0f * t);
                                    };
                                    const auto valueNoise2D = [&](const glm::vec2& p) {
                                        const glm::vec2 i = glm::floor(p);
                                        const glm::vec2 f = glm::fract(p);
                                        const glm::vec2 u = f * f * (glm::vec2(3.0f) - 2.0f * f);
                                        const float a = hash21(i);
                                        const float b = hash21(i + glm::vec2(1.0f, 0.0f));
                                        const float c = hash21(i + glm::vec2(0.0f, 1.0f));
                                        const float d = hash21(i + glm::vec2(1.0f, 1.0f));
                                        return glm::mix(glm::mix(a, b, u.x), glm::mix(c, d, u.x), u.y);
                                    };
                                    const auto fbm2D = [&](glm::vec2 p) {
                                        float v = 0.0f;
                                        float a = 0.5f;
                                        for (int k = 0; k < 5; ++k) {
                                            v += a * valueNoise2D(p);
                                            p *= 2.02f;
                                            a *= 0.5f;
                                        }
                                        return v;
                                    };
                                    const auto fbmGrad = [&](const glm::vec2& p) {
                                        const float e = 0.03f;
                                        const float nx = fbm2D(p + glm::vec2(e, 0.0f)) - fbm2D(p - glm::vec2(e, 0.0f));
                                        const float ny = fbm2D(p + glm::vec2(0.0f, e)) - fbm2D(p - glm::vec2(0.0f, e));
                                        return glm::vec2(nx, ny) / (2.0f * e);
                                    };
                                    const auto curl2D = [&](const glm::vec2& p) {
                                        const glm::vec2 g = fbmGrad(p);
                                        return glm::vec2(g.y, -g.x);
                                    };
                                    const auto advect2D = [&](glm::vec2 p, float flowY, float amount) {
                                        const glm::vec2 c1 = curl2D(p * 1.30f + glm::vec2(0.0f, -flowY * 0.10f));
                                        const glm::vec2 c2 = curl2D(p * 2.70f + glm::vec2(3.1f, -flowY * 0.18f));
                                        return p + (c1 * 0.65f + c2 * 0.35f) * amount;
                                    };
                                    const auto smoothFlicker = [&](float t, float seed) {
                                        const float x = t * 9.0f + seed * 97.0f;
                                        const float i = std::floor(x);
                                        float f = fractf(x);
                                        f = f * f * (3.0f - 2.0f * f);
                                        return glm::mix(hash11(i), hash11(i + 1.0f), f);
                                    };
                                    const auto tonemapSoftLocal = [](const glm::vec3& c) {
                                        return c / (glm::vec3(1.0f) + c);
                                    };
                                    const auto sampleTextureLinear =
                                        [&](const BackendTextureCacheEntry& tex, glm::vec2 uv) -> glm::vec4 {
                                        uv = glm::clamp(uv, glm::vec2(0.0f), glm::vec2(1.0f));
                                        const float x = uv.x * static_cast<float>(std::max(1, tex.width - 1));
                                        const float y = uv.y * static_cast<float>(std::max(1, tex.height - 1));
                                        const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, tex.width - 1);
                                        const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, tex.height - 1);
                                        const int x1 = std::clamp(x0 + 1, 0, tex.width - 1);
                                        const int y1 = std::clamp(y0 + 1, 0, tex.height - 1);
                                        const float tx = x - static_cast<float>(x0);
                                        const float ty = y - static_cast<float>(y0);
                                        const auto sampleAt = [&](int sx, int sy) -> glm::vec4 {
                                            const std::size_t idx =
                                                (static_cast<std::size_t>(sy) * static_cast<std::size_t>(tex.width) +
                                                 static_cast<std::size_t>(sx)) * 4u;
                                            return glm::vec4(
                                                static_cast<float>(tex.rgba[idx + 0u]) / 255.0f,
                                                static_cast<float>(tex.rgba[idx + 1u]) / 255.0f,
                                                static_cast<float>(tex.rgba[idx + 2u]) / 255.0f,
                                                static_cast<float>(tex.rgba[idx + 3u]) / 255.0f);
                                        };
                                        const glm::vec4 c00 = sampleAt(x0, y0);
                                        const glm::vec4 c10 = sampleAt(x1, y0);
                                        const glm::vec4 c01 = sampleAt(x0, y1);
                                        const glm::vec4 c11 = sampleAt(x1, y1);
                                        return glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);
                                    };
                                    const auto sampleAtlasLegacy =
                                        [&](const BackendTextureCacheEntry& tex,
                                            int cols,
                                            int rows,
                                            int frameCount,
                                            float fps,
                                            const glm::vec2& localUV01,
                                            float seed,
                                            float t) -> glm::vec4 {
                                        const int safeCols = std::max(1, cols);
                                        const int safeRows = std::max(1, rows);
                                        const int maxFrames = std::max(1, safeCols * safeRows);
                                        const int frames = std::clamp(frameCount, 1, maxFrames);
                                        const float safeFps = std::max(0.0f, fps);
                                        if (frames <= 1 || safeFps <= 0.0f) {
                                            return sampleTextureLinear(tex, glm::clamp(localUV01, glm::vec2(0.0f), glm::vec2(1.0f)));
                                        }
                                        const float speed = glm::mix(0.85f, 1.10f, hash11(seed * 31.7f + 2.3f));
                                        const float f = std::floor(t * safeFps * speed + seed * static_cast<float>(frames));
                                        float frameF = std::fmod(f, static_cast<float>(frames));
                                        if (frameF < 0.0f) frameF += static_cast<float>(frames);
                                        const int frame = std::clamp(static_cast<int>(frameF), 0, frames - 1);
                                        const int col = frame % safeCols;
                                        const int rowFromTop = frame / safeCols;
                                        const int row = (safeRows - 1) - rowFromTop;
                                        const glm::vec2 cellUV =
                                            (glm::vec2(static_cast<float>(col), static_cast<float>(row)) + localUV01) /
                                            glm::vec2(static_cast<float>(safeCols), static_cast<float>(safeRows));
                                        return sampleTextureLinear(tex, cellUV);
                                    };
                                    const auto lickBlobs =
                                        [&](float x, float y, const glm::vec2& advP, float flowY, float seed) {
                                        const float k = y * 6.6f + flowY * 0.55f;
                                        const float seg = std::floor(k);
                                        const float ff = fractf(k);
                                        const float cx1 = (hash11(seg + seed * 31.0f) - 0.5f) * 0.95f * (1.0f - y);
                                        const float cx2 = (hash11(seg + seed * 73.0f) - 0.5f) * 0.95f * (1.0f - y);
                                        const float w = glm::mix(0.34f, 0.085f, y);
                                        const glm::vec2 q1((x - cx1) / std::max(1e-6f, w),
                                                           (ff - 0.30f) / 0.70f);
                                        const glm::vec2 q2((x - cx2) / std::max(1e-6f, w * 0.85f),
                                                           (ff - 0.45f) / 0.65f);
                                        const float m1 = 1.0f - smoothstepf(0.60f, 1.00f, glm::length(q1 * glm::vec2(1.0f, 1.45f)));
                                        const float m2 = 1.0f - smoothstepf(0.60f, 1.00f, glm::length(q2 * glm::vec2(1.0f, 1.60f)));
                                        const float br = fbm2D(advP * glm::vec2(7.0f, 12.0f) + seed * 17.0f);
                                        const float broken = smoothstepf(0.25f, 0.88f, br);
                                        const float gate =
                                            smoothstepf(0.05f, 0.22f, y) *
                                            (1.0f - smoothstepf(0.86f, 1.0f, y));
                                        return clamp01((m1 + 0.85f * m2) * broken * gate);
                                    };
                                    float tailFireCpuEvalTimeSec = snapshot.timeSec;
                                    const auto evalTailFirePixel =
                                        [&](float age01, float seed, glm::vec2 glPointCoord) -> glm::vec4 {
                                        const float t = tailFireCpuEvalTimeSec;

                                        glm::vec2 uv = glPointCoord;
                                        uv.y = 1.0f - uv.y;
                                        const glm::vec2 cc = (uv - 0.5f) * 2.0f;
                                        const float x = cc.x;
                                        const float y = clamp01(uv.y);

                                        const float bottomFade = smoothstepf(0.00f, 0.11f, y);

                                        const float baseT = smoothstepf(0.00f, 0.22f, y);
                                        const float xScaleBase = glm::mix(2.55f, 1.90f, baseT);
                                        const float yScaleBase = glm::mix(1.05f, 0.75f, baseT);
                                        const float reBase = glm::length(glm::vec2(cc.x * xScaleBase, cc.y * yScaleBase));
                                        const float radialMaskBase = 1.0f - smoothstepf(0.98f, 1.10f, reBase);
                                        const float tightMask = 1.0f - smoothstepf(0.62f, 0.88f, reBase);

                                        const float reLoose = glm::length(cc * glm::vec2(0.55f, 0.85f));
                                        const float radialMaskLoose = 1.0f - smoothstepf(0.98f, 1.20f, reLoose);

                                        float fade = (1.0f - age01);
                                        fade = std::pow(glm::mix(fade, 1.0f, 0.25f), 0.75f);

                                        glm::vec2 wobble(
                                            smoothFlicker(t * 0.9f, seed + 0.17f),
                                            smoothFlicker(t * 1.1f, seed + 0.73f));
                                        wobble -= 0.5f;

                                        const glm::vec2 local1 = uv + wobble * 0.010f;
                                        const glm::vec2 local2 = uv + wobble * 0.002f;

                                        glm::vec4 fb1(1.0f), fb2(1.0f);
                                        const bool has1 = rawAtlasValid(primaryRawTex);
                                        const bool has2 = has1 && rawAtlasValid(secondaryRawTex);
                                        if (has1) {
                                            fb1 = sampleAtlasLegacy(*primaryRawTex,
                                                                    snapshot.flipbookCols,
                                                                    snapshot.flipbookRows,
                                                                    snapshot.flipbookFrames,
                                                                    snapshot.flipbookFps,
                                                                    local1,
                                                                    seed,
                                                                    t);
                                            if (has2) {
                                                fb2 = sampleAtlasLegacy(*secondaryRawTex,
                                                                        snapshot.flipbookCols2,
                                                                        snapshot.flipbookRows2,
                                                                        snapshot.flipbookFrames2,
                                                                        snapshot.flipbookFps2,
                                                                        local2,
                                                                        seed,
                                                                        t);
                                            } else {
                                                fb2 = fb1;
                                            }
                                        }

                                        const float fb1A = clamp01(fb1.a);
                                        const float fb1Lum = clamp01(glm::dot(glm::vec3(fb1), glm::vec3(0.3333f)));

                                        const float speed = glm::mix(0.95f, 1.10f, hash11(seed * 19.31f));
                                        const float flow = t * 1.55f * speed;
                                        const float flowY = flow * glm::mix(0.75f, 1.55f, y * y);

                                        const float width = glm::mix(0.30f, 0.055f, std::pow(y, 2.35f));
                                        const float widthHybrid = width * 2.80f;

                                        float yy = (y * 2.0f - 1.0f);
                                        yy = yy * 1.45f + 0.38f;
                                        yy /= 1.12f;

                                        glm::vec2 p(x / std::max(1e-6f, widthHybrid), yy);
                                        p *= 1.22f;
                                        const float sway = fbm2D(glm::vec2(x * 1.7f, y * 3.8f) +
                                                                 glm::vec2(0.0f, -flowY * 0.65f) +
                                                                 seed * 7.0f);
                                        p.x += (sway - 0.5f) * 0.015f * (1.0f - y);

                                        const float d0 = glm::length(p);
                                        const glm::vec2 advP = advect2D(p * glm::vec2(1.20f, 1.0f) + seed * 6.0f,
                                                                        flowY,
                                                                        0.25f);
                                        const float n = fbm2D(advP * glm::vec2(2.7f, 4.5f) + seed * 11.0f);
                                        const float d = d0 + (n - 0.5f) * 0.18f * (1.0f - y);

                                        const float core = clamp01(1.0f - smoothstepf(0.00f, 0.88f, d));
                                        const float outer = clamp01(1.0f - smoothstepf(0.30f, 1.05f, d));
                                        const float blobs = lickBlobs(x, y, advP, flowY, seed);
                                        const float body = clamp01(smoothstepf(0.92f, 0.12f, d));

                                        float procAlpha = body * (0.60f + 0.55f * blobs);
                                        procAlpha *= (0.92f + 0.15f * smoothFlicker(t * 1.2f, seed));
                                        procAlpha *= bottomFade;
                                        procAlpha *= fade;
                                        procAlpha = 1.0f - std::exp(-procAlpha * 1.85f);
                                        procAlpha = glm::clamp(procAlpha, 0.0f, 0.96f);

                                        const glm::vec3 yellow(1.70f, 1.20f, 0.28f);
                                        const glm::vec3 red(1.45f, 0.18f, 0.06f);
                                        const glm::vec3 orange(1.60f, 0.55f, 0.12f);

                                        const float wave = 0.5f + 0.5f *
                                            std::sin((x * 1.8f + y * 8.5f - flowY * 4.9f) + seed * 7.0f);
                                        const float baseBoundary = 0.34f;
                                        const float segCount = 6.0f;
                                        const float kk = y * segCount - flowY * 0.55f;
                                        const float seg = std::floor(kk);
                                        const float segRand = hash11(seg + seed * 71.3f);
                                        const float segRand2 = hash11(seg + seed * 19.7f + 5.0f);
                                        const float tri1 = std::abs(fractf((x * 0.85f + y * 1.05f - flowY * 0.18f) * 2.8f + seed * 7.0f) - 0.5f) * 2.0f;
                                        const float tri2 = std::abs(fractf((x * 1.10f - y * 0.60f - flowY * 0.14f) * 3.8f + seed * 3.0f) - 0.5f) * 2.0f;
                                        float zig = glm::mix(tri1, tri2, 0.50f + 0.50f * (segRand - 0.5f));
                                        zig = smoothstepf(0.15f, 0.85f, zig);
                                        const float warp = fbm2D(advect2D(glm::vec2(x * 0.85f, y * 1.2f) + seed * 6.0f,
                                                                          flowY,
                                                                          0.22f) *
                                                                 glm::vec2(4.5f, 7.5f)) - 0.5f;

                                        float jag = 0.0f;
                                        jag += (segRand - 0.5f) * 0.10f;
                                        jag += (segRand2 - 0.5f) * 0.05f;
                                        jag += (zig - 0.5f) * 0.14f;
                                        jag += warp * 0.06f;
                                        jag *= (1.0f - 0.55f * smoothstepf(0.65f, 1.0f, y));

                                        const float boundary = glm::clamp(baseBoundary + jag, 0.14f, 0.62f);
                                        const float splitWidth = 0.11f;
                                        const float redMask = smoothstepf(boundary, boundary + splitWidth, y);

                                        glm::vec3 procRgb = glm::mix(yellow, red, redMask);
                                        const float band =
                                            smoothstepf(boundary - 0.02f, boundary + 0.02f, y) *
                                            (1.0f - smoothstepf(boundary + 0.02f, boundary + 0.10f, y));
                                        procRgb = glm::mix(procRgb, orange, 0.55f * band);
                                        const float climb =
                                            core * (1.0f - smoothstepf(0.55f, 0.95f, y)) * (0.35f + 0.65f * wave);
                                        procRgb = glm::mix(procRgb, yellow, 0.18f * climb);
                                        procRgb *= (1.18f + 0.35f * outer);

                                        glm::vec3 hybridRgb = procRgb;
                                        float hybridAlpha = procAlpha;
                                        if (has1) {
                                            const float aMod = glm::mix(0.55f, 1.65f, fb1A);
                                            const float lMod = glm::mix(0.85f, 1.25f, fb1Lum);
                                            hybridAlpha = glm::clamp(hybridAlpha * aMod, 0.0f, 0.96f);
                                            hybridRgb *= lMod;
                                            hybridRgb *= glm::mix(glm::vec3(1.0f), glm::vec3(fb1) * 1.35f, 0.30f);
                                        }

                                        glm::vec3 fb2Rgb = glm::vec3(fb2);
                                        float fb2Alpha = std::pow(clamp01(fb2.a), 0.66f);
                                        const float hot = smoothstepf(0.10f, 0.55f, 1.0f - y);
                                        const glm::vec3 tint = glm::mix(red, yellow, hot);
                                        fb2Rgb *= tint * 1.30f;
                                        fb2Alpha *= tightMask;
                                        fb2Alpha *= bottomFade;

                                        const float hybridMaskedA = hybridAlpha * radialMaskLoose * bottomFade;
                                        const float fb2MaskedA = fb2Alpha * radialMaskBase;

                                        const float mixW = 0.50f;
                                        glm::vec3 rgb = glm::mix(hybridRgb, fb2Rgb, mixW);
                                        float alpha = glm::mix(hybridMaskedA, fb2MaskedA, mixW);
                                        alpha *= fade;
                                        alpha = glm::clamp(alpha + 0.10f * outer * fade, 0.0f, 0.985f);

                                        const float exposure = 2.60f;
                                        rgb *= exposure;
                                        const float emissive = (0.85f * outer + 0.45f * core) * fade;
                                        rgb *= (1.0f + 2.10f * emissive);
                                        rgb = tonemapSoftLocal(rgb);

                                        if (alpha < 0.003f) return glm::vec4(0.0f);
                                        rgb *= alpha; // premultiplied output
                                        return glm::vec4(glm::clamp(rgb, glm::vec3(0.0f), glm::vec3(1.0f)),
                                                         glm::clamp(alpha, 0.0f, 1.0f));
                                    };

                                    auto storePixel = [&](int x, int y, const glm::vec4& c) {
                                        if (x < 0 || y < 0 || x >= atlasW || y >= atlasH) return;
                                        const std::size_t idx =
                                            (static_cast<std::size_t>(y) * static_cast<std::size_t>(atlasW) +
                                             static_cast<std::size_t>(x)) * 4u;
                                        exactBatch.ownedTextureRgba[idx + 0u] = static_cast<unsigned char>(
                                            std::clamp<int>(static_cast<int>(std::lround(clamp01(c.r) * 255.0f)), 0, 255));
                                        exactBatch.ownedTextureRgba[idx + 1u] = static_cast<unsigned char>(
                                            std::clamp<int>(static_cast<int>(std::lround(clamp01(c.g) * 255.0f)), 0, 255));
                                        exactBatch.ownedTextureRgba[idx + 2u] = static_cast<unsigned char>(
                                            std::clamp<int>(static_cast<int>(std::lround(clamp01(c.b) * 255.0f)), 0, 255));
                                        exactBatch.ownedTextureRgba[idx + 3u] = static_cast<unsigned char>(
                                            std::clamp<int>(static_cast<int>(std::lround(clamp01(c.a) * 255.0f)), 0, 255));
                                    };
                                    auto readPixel = [&](int x, int y) -> glm::vec4 {
                                        x = std::clamp(x, 0, atlasW - 1);
                                        y = std::clamp(y, 0, atlasH - 1);
                                        const std::size_t idx =
                                            (static_cast<std::size_t>(y) * static_cast<std::size_t>(atlasW) +
                                             static_cast<std::size_t>(x)) * 4u;
                                        return glm::vec4(
                                            static_cast<float>(exactBatch.ownedTextureRgba[idx + 0u]) / 255.0f,
                                            static_cast<float>(exactBatch.ownedTextureRgba[idx + 1u]) / 255.0f,
                                            static_cast<float>(exactBatch.ownedTextureRgba[idx + 2u]) / 255.0f,
                                            static_cast<float>(exactBatch.ownedTextureRgba[idx + 3u]) / 255.0f);
                                    };

                                    struct TailFireCpuTileCacheEntry {
                                        std::vector<unsigned char> rgba;
                                        std::uint64_t stamp = 0u;
                                    };
                                    static thread_local std::unordered_map<std::uint64_t, TailFireCpuTileCacheEntry>
                                        tailFireCpuTileCache;
                                    static thread_local std::uint64_t tailFireCpuTileCacheStamp = 0u;
                                    ++tailFireCpuTileCacheStamp;
                                    if (tailFireCpuTileCache.size() > 16384u) {
                                        tailFireCpuTileCache.clear();
                                    }

                                    constexpr int kTailFireCpuAgeBins = 16;
                                    constexpr int kTailFireCpuSeedBins = 16;
                                    constexpr int kTailFireCpuTimeFps = 8;
                                    const auto quantizeTailFire01 = [](float v, int bins) {
                                        const int maxBin = std::max(1, bins - 1);
                                        const int q = std::clamp(
                                            static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * maxBin)),
                                            0,
                                            maxBin);
                                        return std::pair<int, float>(q, static_cast<float>(q) / static_cast<float>(maxBin));
                                    };
                                    auto blitCachedTailTile =
                                        [&](const std::vector<unsigned char>& tileRgba, int tileX, int tileY) {
                                        if (tileRgba.size() !=
                                            static_cast<std::size_t>(tilePitch) *
                                                static_cast<std::size_t>(tilePitch) * 4u) {
                                            return false;
                                        }
                                        const int dstX0 = tileX - tilePad;
                                        const int dstY0 = tileY - tilePad;
                                        for (int ly = 0; ly < tilePitch; ++ly) {
                                            const std::size_t srcIdx =
                                                (static_cast<std::size_t>(ly) * static_cast<std::size_t>(tilePitch)) * 4u;
                                            const std::size_t dstIdx =
                                                (static_cast<std::size_t>(dstY0 + ly) * static_cast<std::size_t>(atlasW) +
                                                 static_cast<std::size_t>(dstX0)) * 4u;
                                            std::copy_n(tileRgba.data() + srcIdx,
                                                        static_cast<std::size_t>(tilePitch) * 4u,
                                                        exactBatch.ownedTextureRgba.data() + dstIdx);
                                        }
                                        return true;
                                    };
                                    auto captureTailTileToCache =
                                        [&](TailFireCpuTileCacheEntry& entry, int tileX, int tileY) {
                                        entry.rgba.assign(
                                            static_cast<std::size_t>(tilePitch) * static_cast<std::size_t>(tilePitch) * 4u,
                                            0u);
                                        const int srcX0 = tileX - tilePad;
                                        const int srcY0 = tileY - tilePad;
                                        for (int ly = 0; ly < tilePitch; ++ly) {
                                            const std::size_t srcIdx =
                                                (static_cast<std::size_t>(srcY0 + ly) * static_cast<std::size_t>(atlasW) +
                                                 static_cast<std::size_t>(srcX0)) * 4u;
                                            const std::size_t dstIdx =
                                                (static_cast<std::size_t>(ly) * static_cast<std::size_t>(tilePitch)) * 4u;
                                            std::copy_n(exactBatch.ownedTextureRgba.data() + srcIdx,
                                                        static_cast<std::size_t>(tilePitch) * 4u,
                                                        entry.rgba.data() + dstIdx);
                                        }
                                    };

                                    bool appendedAnyExact = false;
                                    for (std::size_t pi = 0; pi < snapshot.particles.size(); ++pi) {
                                        const auto& particle = snapshot.particles[pi];
                                        const float maxLife = std::max(0.0001f, particle.maxLifeSec);
                                        float age01 = 1.0f - (particle.lifeSec / maxLife);
                                        age01 = std::clamp(age01, 0.0f, 1.0f);

                                        const glm::vec4 clip = viewProj * glm::vec4(particle.pos, 1.0f);
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
                                        const float halfNdcX = pxSize / std::max(1, drawableW);
                                        const float halfNdcY = pxSize / std::max(1, drawableH);
                                        if (halfNdcX <= 0.000001f || halfNdcY <= 0.000001f) continue;

                                        const float ndcX = clip.x / clip.w;
                                        const float ndcY = clip.y / clip.w;
                                        glm::vec3 corners[4];
                                        if (!safeUnprojectClip(glm::vec4((ndcX - halfNdcX) * clip.w,
                                                                         (ndcY - halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[0]) ||
                                            !safeUnprojectClip(glm::vec4((ndcX + halfNdcX) * clip.w,
                                                                         (ndcY - halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[1]) ||
                                            !safeUnprojectClip(glm::vec4((ndcX + halfNdcX) * clip.w,
                                                                         (ndcY + halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[2]) ||
                                            !safeUnprojectClip(glm::vec4((ndcX - halfNdcX) * clip.w,
                                                                         (ndcY + halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[3])) {
                                            continue;
                                        }

                                        const int tileIndex = static_cast<int>(pi);
                                        const int tileCol = tileIndex % atlasCols;
                                        const int tileRow = tileIndex / atlasCols;
                                        const int tileX = tileCol * tilePitch + tilePad;
                                        const int tileY = tileRow * tilePitch + tilePad;
                                        const float seed = std::clamp(particle.seed, 0.0f, 1.0f);
                                        const auto [ageQ, ageEval] = quantizeTailFire01(age01, kTailFireCpuAgeBins);
                                        const auto [seedQ, seedEval] = quantizeTailFire01(seed, kTailFireCpuSeedBins);
                                        const int timeQ = (static_cast<int>(std::floor(snapshot.timeSec *
                                                                                        static_cast<float>(kTailFireCpuTimeFps))) &
                                                          0x7ff);
                                        tailFireCpuEvalTimeSec =
                                            static_cast<float>(timeQ) / static_cast<float>(kTailFireCpuTimeFps);
                                        const std::uint64_t cacheKey =
                                            (static_cast<std::uint64_t>(tileSize & 0xff) << 0) |
                                            (static_cast<std::uint64_t>(ageQ & 0xff) << 8) |
                                            (static_cast<std::uint64_t>(seedQ & 0xff) << 16) |
                                            (static_cast<std::uint64_t>(timeQ & 0x7ff) << 24) |
                                            (static_cast<std::uint64_t>(rawAtlasValid(secondaryRawTex) ? 1u : 0u) << 35);
                                        bool usedCachedTile = false;
                                        auto cacheIt = tailFireCpuTileCache.find(cacheKey);
                                        if (cacheIt != tailFireCpuTileCache.end()) {
                                            cacheIt->second.stamp = tailFireCpuTileCacheStamp;
                                            usedCachedTile = blitCachedTailTile(cacheIt->second.rgba, tileX, tileY);
                                        }

                                        if (!usedCachedTile) {
                                            for (int ty = 0; ty < tileSize; ++ty) {
                                                for (int tx = 0; tx < tileSize; ++tx) {
                                                    const glm::vec2 glPointCoord(
                                                        (static_cast<float>(tx) + 0.5f) / static_cast<float>(tileSize),
                                                        (static_cast<float>(ty) + 0.5f) / static_cast<float>(tileSize));
                                                    storePixel(
                                                        tileX + tx,
                                                        tileY + ty,
                                                        evalTailFirePixel(ageEval, seedEval, glPointCoord));
                                                }
                                            }
                                            // Duplicate edge texels into the tile padding to avoid linear-filter bleed.
                                            for (int tx = 0; tx < tileSize; ++tx) {
                                                const glm::vec4 topPx = readPixel(tileX + tx, tileY);
                                                const glm::vec4 botPx = readPixel(tileX + tx, tileY + tileSize - 1);
                                                storePixel(tileX + tx, tileY - 1, topPx);
                                                storePixel(tileX + tx, tileY + tileSize, botPx);
                                            }
                                            for (int ty = 0; ty < tileSize; ++ty) {
                                                const glm::vec4 leftPx = readPixel(tileX, tileY + ty);
                                                const glm::vec4 rightPx = readPixel(tileX + tileSize - 1, tileY + ty);
                                                storePixel(tileX - 1, tileY + ty, leftPx);
                                                storePixel(tileX + tileSize, tileY + ty, rightPx);
                                            }
                                            storePixel(tileX - 1, tileY - 1, readPixel(tileX, tileY));
                                            storePixel(tileX + tileSize, tileY - 1, readPixel(tileX + tileSize - 1, tileY));
                                            storePixel(tileX - 1, tileY + tileSize, readPixel(tileX, tileY + tileSize - 1));
                                            storePixel(tileX + tileSize,
                                                      tileY + tileSize,
                                                      readPixel(tileX + tileSize - 1, tileY + tileSize - 1));

                                            auto& cacheEntry = tailFireCpuTileCache[cacheKey];
                                            cacheEntry.stamp = tailFireCpuTileCacheStamp;
                                            captureTailTileToCache(cacheEntry, tileX, tileY);
                                        }

                                        const float u0 = (static_cast<float>(tileX) + 0.5f) / static_cast<float>(atlasW);
                                        const float v0 = (static_cast<float>(tileY) + 0.5f) / static_cast<float>(atlasH);
                                        const float u1 = (static_cast<float>(tileX + tileSize) - 0.5f) / static_cast<float>(atlasW);
                                        const float v1 = (static_cast<float>(tileY + tileSize) - 0.5f) / static_cast<float>(atlasH);

                                        const std::uint32_t baseVertex =
                                            static_cast<std::uint32_t>(exactBatch.vertices.size());
                                        const auto pushVertex = [&](const glm::vec3& p, float u, float v) {
                                            IRenderBackend::WorldMeshVertex vtx;
                                            vtx.x = p.x;
                                            vtx.y = p.y;
                                            vtx.z = p.z;
                                            vtx.u = u;
                                            vtx.v = v;
                                            vtx.r = 1.0f;
                                            vtx.g = 1.0f;
                                            vtx.b = 1.0f;
                                            vtx.a = 1.0f;
                                            exactBatch.vertices.push_back(vtx);
                                        };
                                        // Exact tail-fire tiles are CPU-baked into a top-down row-major atlas.
                                        // Flip V here so the shared billboard reads the same orientation as the
                                        // legacy point-sprite shader output.
                                        pushVertex(corners[0], u0, v1);
                                        pushVertex(corners[1], u1, v1);
                                        pushVertex(corners[2], u1, v0);
                                        pushVertex(corners[3], u0, v0);
                                        exactBatch.indices.push_back(baseVertex + 0u);
                                        exactBatch.indices.push_back(baseVertex + 1u);
                                        exactBatch.indices.push_back(baseVertex + 2u);
                                        exactBatch.indices.push_back(baseVertex + 0u);
                                        exactBatch.indices.push_back(baseVertex + 2u);
                                        exactBatch.indices.push_back(baseVertex + 3u);
                                        const float distSq =
                                            glm::dot(cameraWorldPos - particle.pos, cameraWorldPos - particle.pos);
                                        exactBatch.sortDepth = std::max(exactBatch.sortDepth, distSq);
                                        appendedAnyExact = true;
                                    }

                                    if (appendedAnyExact &&
                                        !exactBatch.vertices.empty() &&
                                        !exactBatch.indices.empty() &&
                                        !exactBatch.ownedTextureRgba.empty()) {
                                        worldIndexedBatches.push_back(std::move(exactBatch));
                                        return true;
                                    }
                                }

                                auto initParticleBatch =
                                    [&](WorldIndexedBatch& batch,
                                        const char* passName,
                                        const std::string& texPath,
                                        const BackendTextureCacheEntry& texRef) {
                                        batch = {};
                                        batch.textureKey =
                                            std::string("particle:") + label + ":" + passName + ":" + texPath;
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
                                const bool hasSecondary =
                                    (secondaryTex && secondaryTex->valid && !secondaryTex->rgba.empty());
                                if (hasSecondary) {
                                    initParticleBatch(coreBatch, "tail_fire_core", snapshot.flipbookPath2, *secondaryTex);
                                }

                                auto computeTailFireFrameUv =
                                    [&](const ParticleSystem::Particle& particle,
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
                                            u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
                                            return;
                                        }

                                        const float seed = std::clamp(particle.seed, 0.0f, 1.0f);
                                        const float speedNoise = hashFrac01(seed * 31.7f + 2.3f);
                                        const float speed = glm::mix(0.85f, 1.10f, speedNoise);
                                        const float f = std::floor(snapshot.timeSec * fps * speed + seed * static_cast<float>(frames));
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

                                auto appendBillboardToBatch =
                                    [&](WorldIndexedBatch& batch,
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
                                        const float halfNdcX = px / std::max(1, drawableW);
                                        const float halfNdcY = px / std::max(1, drawableH);
                                        if (halfNdcX <= 0.000001f || halfNdcY <= 0.000001f) return false;

                                        const float ndcX = clip.x / clip.w;
                                        const float ndcY = clip.y / clip.w;
                                        glm::vec3 corners[4];
                                        if (!safeUnprojectClip(glm::vec4((ndcX - halfNdcX) * clip.w,
                                                                         (ndcY - halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[0]) ||
                                            !safeUnprojectClip(glm::vec4((ndcX + halfNdcX) * clip.w,
                                                                         (ndcY - halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[1]) ||
                                            !safeUnprojectClip(glm::vec4((ndcX + halfNdcX) * clip.w,
                                                                         (ndcY + halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[2]) ||
                                            !safeUnprojectClip(glm::vec4((ndcX - halfNdcX) * clip.w,
                                                                         (ndcY + halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[3])) {
                                            return false;
                                        }

                                        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
                                        computeTailFireFrameUv(particle, secondaryAtlas, u0, v0, u1, v1);

                                        const std::uint32_t baseVertex =
                                            static_cast<std::uint32_t>(batch.vertices.size());
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
                                        const float distSq =
                                            glm::dot(cameraWorldPos - particlePos, cameraWorldPos - particlePos);
                                        batch.sortDepth = std::max(batch.sortDepth, distSq);
                                        return true;
                                    };

                                bool appendedAny = false;
                                for (const auto& particle : snapshot.particles) {
                                    const float maxLife = std::max(0.0001f, particle.maxLifeSec);
                                    float age01 = 1.0f - (particle.lifeSec / maxLife);
                                    age01 = std::clamp(age01, 0.0f, 1.0f);

                                    const glm::vec4 clip = viewProj * glm::vec4(particle.pos, 1.0f);
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

                                    const float seed = std::clamp(particle.seed, 0.0f, 1.0f);
                                    const float fade = std::pow(glm::mix(1.0f - age01, 1.0f, 0.25f), 0.75f);
                                    const float flicker =
                                        glm::mix(0.92f,
                                                 1.08f,
                                                 hashFrac01(std::floor(snapshot.timeSec * 11.0f) + seed * 91.0f));
                                    glm::vec3 hybridColor = tailFireRampOrangeRed(age01);
                                    hybridColor *= (0.95f + 0.15f * flicker);
                                    hybridColor = glm::clamp(hybridColor, glm::vec3(0.0f), glm::vec3(1.0f));
                                    const float hybridAlpha = std::clamp(
                                        (0.74f + 0.30f * (1.0f - age01)) * fade * (0.98f + 0.12f * flicker),
                                        0.0f,
                                        0.95f);
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
                                            (0.62f + 0.24f * (1.0f - age01)) * fade * (0.98f + 0.12f * flicker),
                                            0.0f,
                                            0.90f);
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

                            std::string texturePath = "__proc:soft_circle";
                            if (snapshot.useFlipbook && !snapshot.flipbookPath.empty()) {
                                texturePath = snapshot.flipbookPath;
                            } else {
                                if (frag.find("leaf_impact") != std::string::npos) texturePath = "__proc:leaf";
                                else if (frag.find("splat_impact") != std::string::npos) texturePath = "__proc:starburst";
                                else if (frag.find("impact_spark") != std::string::npos) texturePath = "__proc:dot";
                                else if (frag.find("claw_swipe") != std::string::npos) texturePath = "__proc:claw";
                                else if (frag.find("aqua_swoosh") != std::string::npos) texturePath = "__proc:swoosh";
                                else if (frag.find("seed_projectile") != std::string::npos) texturePath = "__proc:seed";
                                else if (frag.find("leech_drain_dot") != std::string::npos) texturePath = "__proc:dot";
                                else if (frag.find("heal_plus") != std::string::npos) texturePath = "__proc:plus";
                            }

                            BackendTextureCacheEntry* tex = ensureBackendTextureLoaded(texturePath);
                            if (!tex || !tex->valid || tex->rgba.empty()) {
                                tex = ensureBackendTextureLoaded("");
                            }
                            if (!tex || !tex->valid || tex->rgba.empty()) return false;

                            WorldIndexedBatch batch;
                            batch.textureKey = std::string("particle:") + label + ":" + texturePath;
                            batch.textureRgba = tex->rgba.data();
                            batch.textureWidth = tex->width;
                            batch.textureHeight = tex->height;
                            batch.textureWrapS = 33071; // clamp
                            batch.textureWrapT = 33071; // clamp
                            batch.alphaMode = 2u;
                            batch.blendMode = blendMode;
                            batch.alphaCutoff = 0.0f;
                            batch.sortDepth = 0.0f;
                            game::runtime::shared_particle_billboards::BuildContext billboardCtx;
                            billboardCtx.viewProj = viewProj;
                            billboardCtx.invViewProj = invViewProj;
                            billboardCtx.cameraWorldPos = cameraWorldPos;
                            billboardCtx.drawableW = drawableW;
                            billboardCtx.drawableH = drawableH;
                            return game::runtime::shared_particle_billboards::appendGenericBatch(
                                snapshot,
                                billboardCtx,
                                std::move(batch),
                                worldIndexedBatches);
                        };

    return appendSnapshotAsBillboards(label, snapshot);
}

} // namespace game::runtime::shared_particle_snapshot_billboards
