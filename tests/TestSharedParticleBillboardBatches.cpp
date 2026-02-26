#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"
#include "game/runtime/shared/SharedParticleBillboardBatches.h"

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

bool test_shared_particle_billboard_batches_contract(std::string& outFail) {
    using game::runtime::shared_particle_billboards::appendGenericBatch;
    using game::runtime::shared_particle_billboards::BuildContext;
    using game::runtime::shared_world_batches::WorldIndexedBatch;

    ParticleSystem::RenderSnapshot snapshot;
    snapshot.shaderFragPath = "assets/shaders/vfx/heal_plus.frag";
    snapshot.pointScale = 120.0f;
    snapshot.useFlipbook = false;

    ParticleSystem::Particle particle;
    particle.pos = glm::vec3(0.0f, 0.0f, 0.0f);
    particle.lifeSec = 0.25f;
    particle.maxLifeSec = 1.0f;
    particle.sizePx = 16.0f;
    particle.seed = 0.4f;
    snapshot.particles.push_back(particle);

    BuildContext ctx;
    ctx.viewProj = glm::mat4(1.0f);
    ctx.invViewProj = glm::mat4(1.0f);
    ctx.cameraWorldPos = glm::vec3(0.0f, 0.0f, 5.0f);
    ctx.drawableW = 1280;
    ctx.drawableH = 720;

    static const unsigned char kWhite[4] = {255u, 255u, 255u, 255u};
    WorldIndexedBatch batchTemplate;
    batchTemplate.textureKey = "particle:test:__proc:plus";
    batchTemplate.textureRgba = kWhite;
    batchTemplate.textureWidth = 1;
    batchTemplate.textureHeight = 1;
    batchTemplate.textureWrapS = 33071;
    batchTemplate.textureWrapT = 33071;
    batchTemplate.alphaMode = 2u;
    batchTemplate.blendMode = 0u;
    batchTemplate.alphaCutoff = 0.0f;

    std::vector<WorldIndexedBatch> out;
    const bool appended = appendGenericBatch(snapshot, ctx, batchTemplate, out);
    if (!expect(appended, "appendGenericBatch should append a batch for a visible particle.", outFail)) {
        return false;
    }
    if (!expect(out.size() == 1u, "appendGenericBatch should append exactly one batch.", outFail)) {
        return false;
    }
    if (!expect(out[0].vertices.size() == 4u && out[0].indices.size() == 6u,
                "appendGenericBatch should emit one billboard quad (4 verts / 6 indices) per visible particle.",
                outFail)) {
        return false;
    }
    if (!expect(out[0].sortDepth > 0.0f,
                "appendGenericBatch should accumulate sort depth from camera-to-particle distance.",
                outFail)) {
        return false;
    }
    if (!expect(out[0].vertices[0].g >= out[0].vertices[0].r,
                "appendGenericBatch should preserve shared particle style tint (heal-plus should be green-dominant).",
                outFail)) {
        return false;
    }

    ParticleSystem::RenderSnapshot flipbook = snapshot;
    flipbook.shaderFragPath = "assets/shaders/vfx/particle.frag";
    flipbook.useFlipbook = true;
    flipbook.flipbookPath = "assets/vfx/atlas.png";
    flipbook.flipbookCols = 2;
    flipbook.flipbookRows = 2;
    flipbook.flipbookFrames = 4;
    flipbook.particles.clear();
    ParticleSystem::Particle p2 = particle;
    p2.lifeSec = 0.0f; // age01 = 1.0 -> last frame
    flipbook.particles.push_back(p2);

    out.clear();
    if (!expect(appendGenericBatch(flipbook, ctx, batchTemplate, out),
                "appendGenericBatch should append flipbook billboards too.",
                outFail)) {
        return false;
    }
    if (!expect(out.size() == 1u && out[0].vertices.size() == 4u,
                "appendGenericBatch flipbook path should still emit one quad.",
                outFail)) {
        return false;
    }
    if (!expect(approx(out[0].vertices[0].u, 0.5f) && approx(out[0].vertices[0].v, 0.5f),
                "appendGenericBatch should compute last-frame flipbook UVs for age01=1 on a 2x2 atlas.",
                outFail)) {
        return false;
    }
    if (!expect(approx(out[0].vertices[2].u, 1.0f) && approx(out[0].vertices[2].v, 1.0f),
                "appendGenericBatch should emit correct UV max for the selected flipbook frame.",
                outFail)) {
        return false;
    }

    ParticleSystem::RenderSnapshot offscreen = snapshot;
    offscreen.particles.clear();
    ParticleSystem::Particle p3 = particle;
    p3.pos = glm::vec3(0.0f, 0.0f, 2.5f); // ndc z > 1.2 with identity VP
    offscreen.particles.push_back(p3);
    out.clear();
    if (!expect(!appendGenericBatch(offscreen, ctx, batchTemplate, out),
                "appendGenericBatch should no-op for particles outside clip depth range.",
                outFail)) {
        return false;
    }
    if (!expect(out.empty(),
                "appendGenericBatch should not append a batch when all particles are rejected.",
                outFail)) {
        return false;
    }

    return true;
}
