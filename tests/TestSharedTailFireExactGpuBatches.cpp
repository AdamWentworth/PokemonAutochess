#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireExactGpuBatches.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool approx(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool test_shared_tail_fire_exact_gpu_batches_contract(std::string& outFail) {
    using namespace game::runtime::shared_tail_fire_exact_gpu;
    using game::runtime::shared_world_batches::WorldIndexedBatch;

    ParticleSystem::RenderSnapshot snapshot;
    snapshot.useFlipbook = true;
    snapshot.flipbookPath = "assets/vfx/fire_tail_primary.png";
    snapshot.flipbookCols = 4;
    snapshot.flipbookRows = 4;
    snapshot.flipbookFrames = 16;
    snapshot.flipbookFps = 24.0f;
    snapshot.useSecondaryFlipbook = true;
    snapshot.flipbookPath2 = "assets/vfx/fire_tail_secondary.png";
    snapshot.flipbookCols2 = 2;
    snapshot.flipbookRows2 = 2;
    snapshot.flipbookFrames2 = 4;
    snapshot.flipbookFps2 = 12.0f;
    snapshot.pointScale = 100.0f;
    snapshot.timeSec = 1.25f;

    ParticleSystem::Particle p;
    p.pos = glm::vec3(0.0f, 0.0f, 0.0f);
    p.lifeSec = 0.25f;
    p.maxLifeSec = 1.0f;
    p.sizePx = 16.0f;
    p.seed = 0.4f;
    snapshot.particles.push_back(p);

    std::vector<unsigned char> atlasRgba(8u * 4u * 4u, 255u);
    AtlasView atlas;
    atlas.rgba = atlasRgba.data();
    atlas.width = 8;
    atlas.height = 4;
    atlas.cacheKey = "__tailfire_combined_exact__:a|b";
    atlas.rect0 = glm::vec4(0.0f, 0.0f, 0.5f, 1.0f);
    atlas.rect1 = glm::vec4(0.75f, 0.0f, 0.25f, 1.0f);
    atlas.hasSecondary = true;

    BuildContext ctx;
    ctx.viewProj = glm::mat4(1.0f);
    ctx.invViewProj = glm::mat4(1.0f);
    ctx.cameraWorldPos = glm::vec3(0.0f, 0.0f, 5.0f);
    ctx.drawableW = 1280;
    ctx.drawableH = 720;
    ctx.blendMode = 1u;

    std::vector<WorldIndexedBatch> out;
    if (!expect(appendBatch("tail_fire", snapshot, ctx, atlas, out),
                "appendBatch should emit an exact tail-fire batch for a visible particle.",
                outFail)) {
        return false;
    }
    if (!expect(out.size() == 1u, "appendBatch should append exactly one batch.", outFail)) {
        return false;
    }
    const WorldIndexedBatch& batch = out[0];
    if (!expect(batch.vertices.size() == 4u && batch.indices.size() == 6u,
                "appendBatch should emit one billboard quad (4 verts / 6 indices) per visible particle.",
                outFail)) {
        return false;
    }
    if (!expect(batch.materialMode == 1u && batch.alphaMode == 2u && batch.blendMode == 1u,
                "appendBatch should configure the exact fire-tail material and preserve requested blend mode.",
                outFail)) {
        return false;
    }
    if (!expect(batch.textureWidth == 8 && batch.textureHeight == 4 &&
                    batch.textureRgba == atlasRgba.data(),
                "appendBatch should forward atlas texture payload/dimensions without copying.",
                outFail)) {
        return false;
    }
    if (!expect(batch.textureKey == "particle:tail_fire:tail_fire_exact_gpu:__tailfire_combined_exact__:a|b",
                "appendBatch should build a stable exact tail-fire cache key.",
                outFail)) {
        return false;
    }
    if (!expect(approx(batch.materialRect0W, 0.5f) && approx(batch.materialRect1U, 0.75f) &&
                    approx(batch.materialFlags, 3.0f),
                "appendBatch should forward atlas rects and set secondary-atlas material flag.",
                outFail)) {
        return false;
    }
    if (!expect(batch.sortDepth > 0.0f,
                "appendBatch should accumulate sort depth from camera-to-particle distance.",
                outFail)) {
        return false;
    }
    if (!expect(batch.vertices[0].r >= 0.7f && batch.vertices[0].r <= 0.8f &&
                    approx(batch.vertices[0].g, 0.4f),
                "appendBatch should encode age01 and seed into vertex color channels.",
                outFail)) {
        return false;
    }

    ParticleSystem::RenderSnapshot offscreen = snapshot;
    offscreen.particles.clear();
    p.pos = glm::vec3(0.0f, 0.0f, 2.5f); // ndc z > 1.2 with identity VP
    offscreen.particles.push_back(p);
    out.clear();
    if (!expect(!appendBatch("tail_fire", offscreen, ctx, atlas, out),
                "appendBatch should no-op when all particles are rejected by clip/depth tests.",
                outFail)) {
        return false;
    }
    if (!expect(out.empty(),
                "appendBatch should not append a batch when all particles are rejected.",
                outFail)) {
        return false;
    }

    out.clear();
    AtlasView invalidAtlas;
    if (!expect(!appendBatch("tail_fire", snapshot, ctx, invalidAtlas, out),
                "appendBatch should fail cleanly on invalid atlas input.",
                outFail)) {
        return false;
    }

    return true;
}

