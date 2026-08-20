#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"

namespace {

game::runtime::SharedBackendTextureCacheEntry makeTexture() {
    game::runtime::SharedBackendTextureCacheEntry out;
    out.attemptedLoad = true;
    out.valid = true;
    out.width = 2;
    out.height = 2;
    out.rgba.resize(16u, 255u);
    return out;
}

} // namespace

bool test_shared_particle_snapshot_billboards_contract(std::string& outFail) {
    ParticleSystem::RenderSnapshot snapshot;
    snapshot.shaderFragPath = "assets/shaders/vfx/heal_plus.frag";
    snapshot.pointScale = 120.0f;

    ParticleSystem::Particle particle;
    particle.pos = glm::vec3(0.0f, 0.0f, 0.0f);
    particle.lifeSec = 0.25f;
    particle.maxLifeSec = 1.0f;
    particle.sizePx = 16.0f;
    particle.seed = 0.4f;
    snapshot.particles.push_back(particle);

    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry> cache;
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> batches;
    const bool ok = game::runtime::shared_particle_snapshot_billboards::appendSnapshotAsBillboards(
        "tackle_impact",
        snapshot,
        glm::mat4(1.0f),
        glm::mat4(1.0f),
        glm::vec3(0.0f, 0.0f, 5.0f),
        1280,
        720,
        [&](const std::string& texturePath, bool) -> game::runtime::SharedBackendTextureCacheEntry* {
            const std::string key = texturePath.empty() ? "__white__" : texturePath;
            auto it = cache.find(key);
            if (it == cache.end()) {
                it = cache.emplace(key, makeTexture()).first;
            }
            return &it->second;
        },
        batches);

    if (!ok || batches.size() != 1u) {
        outFail = "SharedParticleSnapshotBillboards should emit one indexed batch for a simple particle snapshot.";
        return false;
    }

    const auto& batch = batches.front();
    if (batch.textureKey != "particle:tackle_impact:__proc:plus") {
        outFail = "SharedParticleSnapshotBillboards should preserve the label-qualified particle texture key.";
        return false;
    }

    if (batch.textureCacheKey != "__particle_shared__:__proc:plus") {
        outFail = "SharedParticleSnapshotBillboards should set a shared cache key so equivalent particle textures reuse backend uploads.";
        return false;
    }

    return true;
}
