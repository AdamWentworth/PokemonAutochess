#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

#include "game/runtime/shared/vfx/particles/SharedParticleBillboardBatches.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSnapshotBillboards.h"

namespace game::runtime::shared_particle_snapshot_billboards {
namespace {

const std::vector<std::string> kCommonParticleTexturePaths{
    "__proc:soft_circle",
    "__proc:leaf",
    "__proc:starburst",
    "__proc:dot",
    "__proc:claw",
    "__proc:swoosh",
    "__proc:seed",
    "__proc:plus",
};

std::string toLowerCopyLocal(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

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

} // namespace

std::string makeSharedParticleTextureCacheKey(const std::string& texturePath) {
    return "__particle_shared__:" + texturePath;
}

const std::vector<std::string>& commonParticleTexturePaths() {
    return kCommonParticleTexturePaths;
}

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
    const std::unordered_map<int, shared_tail_fire_fallback::Anchor>* tailFireAnchors,
    bool tailFireExactCpuEnabled,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches) {
    using BackendTextureCacheEntry = SharedBackendTextureCacheEntry;
    using WorldIndexedBatch = shared_world_batches::WorldIndexedBatch;

    auto ensureBackendTextureLoaded =
        [&](const std::string& texturePath, bool flipVertical = false) -> BackendTextureCacheEntry* {
            if (!ensureTextureFn) return nullptr;
            return ensureTextureFn(texturePath, flipVertical);
        };

    if (!label) return false;
    if (snapshot.particles.empty()) return false;

    const std::uint8_t blendMode = toBackendBlendMode(snapshot.renderSettings.blend);
    const std::string frag = toLowerCopyLocal(snapshot.shaderFragPath);
    const bool tailFireShader = (frag.find("fire_tail") != std::string::npos);

    if (tailFireShader) {
        game::runtime::shared_tail_fire_snapshot_billboards::AppendContext tailCtx{
            viewProj,
            invViewProj,
            cameraWorldPos,
            drawableW,
            drawableH,
            backendTextureByPath,
            ensureTextureFn,
            tailFireAnchors,
            tailFireExactCpuEnabled};
        return game::runtime::shared_tail_fire_snapshot_billboards::appendTailFireSnapshotBillboards(
            label, snapshot, blendMode, tailCtx, worldIndexedBatches);
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
    batch.textureCacheKey = makeSharedParticleTextureCacheKey(texturePath);
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
        snapshot, billboardCtx, std::move(batch), worldIndexedBatches);
}

} // namespace game::runtime::shared_particle_snapshot_billboards

