#include "game/runtime/startup/RuntimeParticleVfxPrewarm.h"

#include "engine/render/IRenderBackend.h"
#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <vector>

namespace game::runtime::particle_vfx_prewarm {

namespace {

IRenderBackend::WorldMeshVertex makeVertex(float x, float y, float z, float u, float v) {
    IRenderBackend::WorldMeshVertex out;
    out.x = x;
    out.y = y;
    out.z = z;
    out.u = u;
    out.v = v;
    return out;
}

shared_world_batches::WorldIndexedBatch makeBatch(const std::string& texturePath,
                                                  const SharedBackendTextureCacheEntry& texture) {
    shared_world_batches::WorldIndexedBatch batch;
    batch.vertices = {
        makeVertex(-0.25f, 0.0f, -0.25f, 0.0f, 1.0f),
        makeVertex(0.25f, 0.0f, -0.25f, 1.0f, 1.0f),
        makeVertex(0.25f, 0.0f, 0.25f, 1.0f, 0.0f),
        makeVertex(-0.25f, 0.0f, 0.25f, 0.0f, 0.0f),
    };
    batch.indices = {0u, 1u, 2u, 0u, 2u, 3u};
    batch.textureKey = std::string("particle:prewarm:") + texturePath;
    batch.textureCacheKey =
        shared_particle_snapshot_billboards::makeSharedParticleTextureCacheKey(texturePath);
    batch.textureRgba = texture.rgba.data();
    batch.textureWidth = texture.width;
    batch.textureHeight = texture.height;
    batch.textureWrapS = 33071;
    batch.textureWrapT = 33071;
    batch.alphaMode = 2u;
    batch.blendMode = 0u;
    return batch;
}

} // namespace

startup_asset_prewarm::ParticleVfxStats prewarm(const Args& args) {
    startup_asset_prewarm::ParticleVfxStats stats;
    if (!args.renderer || !args.ensureBackendTextureLoaded) {
        return stats;
    }
    if (!args.renderer->supportsWorldIndexedMeshes()) {
        return stats;
    }

    std::vector<shared_world_batches::WorldIndexedBatch> batches;
    batches.reserve(shared_particle_snapshot_billboards::commonParticleTexturePaths().size());
    for (const std::string& texturePath :
         shared_particle_snapshot_billboards::commonParticleTexturePaths()) {
        SharedBackendTextureCacheEntry* texture =
            args.ensureBackendTextureLoaded(texturePath, false);
        if (!texture || !texture->valid || texture->rgba.empty() ||
            texture->width <= 0 || texture->height <= 0) {
            continue;
        }

        batches.push_back(makeBatch(texturePath, *texture));
        ++stats.textures;
    }

    stats.warmedBatches =
        shared_world_batches::prewarmWorldIndexedBatches(*args.renderer, batches);
    return stats;
}

} // namespace game::runtime::particle_vfx_prewarm
