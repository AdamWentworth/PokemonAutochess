#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"

#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxInterop.h"
#include "vfx/runtime/shared/SharedAuthoredVfxBridge.h"
#include "vfx/runtime/shared/SharedAuthoredVfxHelpers.h"

#include <unordered_map>

namespace game::runtime::shared_projected_scene {

void appendSharedTackleSmokeVfx(const TackleSmokeVfxArgs& args) {
    if (!args.supportsWorldIndexedMeshes || !args.hasWorldViewProj) return;
    if (!args.gameWorld || !args.worldIndexedBatches || !args.backendTextureByPath) return;
    if (!args.ensureBackendMeshLoaded || !args.ensureBackendTextureLoaded) return;
    if (args.gameWorld->countActiveTackleSmokeVfx() == 0u) return;

    TackleSmokeVFX::RenderSnapshot tackleSnapshot;
    if (!args.gameWorld->buildTackleSmokeSnapshot(tackleSnapshot)) return;
    if (tackleSnapshot.drawPasses.empty() || tackleSnapshot.rings.empty()) return;

    using AuthoredTevState = vfx::runtime::authored::TevState;

    const auto resolveTackleSharedTexture =
        [&](const SharedAuthoredBatchVFX::Config::DrawPass& pass,
            const AuthoredTevState& tev) -> SharedBackendTextureCacheEntry* {
            if (vfx::runtime::authored::isLinePass(tackleSnapshot.config, pass) ||
                pass.texturePath.empty()) {
                return args.ensureBackendTextureLoaded("", false);
            }

            SharedBackendTextureCacheEntry* rawTex =
                args.ensureBackendTextureLoaded(pass.texturePath, false);
            if (!rawTex || !rawTex->valid || rawTex->rgba.empty() ||
                rawTex->width <= 0 || rawTex->height <= 0) {
                return nullptr;
            }

            const bool quarterPass =
                vfx::runtime::authored::isQuarterRingPass(tackleSnapshot.config, pass);
            auto& backendTextureByPath = *args.backendTextureByPath;
            if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
            const std::string bakedKey =
                vfx::runtime::authored::makeBakedTextureKey(pass, quarterPass);
            auto& baked = backendTextureByPath[bakedKey];
            if (baked.attemptedLoad) {
                return baked.valid ? &baked : nullptr;
            }

            baked.attemptedLoad = true;
            baked.valid = false;
            baked.width = rawTex->width;
            baked.height = rawTex->height;
            baked.rgba.clear();
            if (!vfx::runtime::authored::bakePassTextureRgba(
                    pass, tev, quarterPass, rawTex->rgba, baked.rgba)) {
                return nullptr;
            }

            baked.valid = true;
            return &baked;
        };
    const auto resolveTackleTextureView =
        [&](const SharedAuthoredBatchVFX::Config::DrawPass& pass,
            const AuthoredTevState& tev,
            vfx::runtime::authored_batches::TextureView& outView) {
            SharedBackendTextureCacheEntry* tex = resolveTackleSharedTexture(pass, tev);
            if (!tex) tex = args.ensureBackendTextureLoaded("", false);
            if (!tex || !tex->valid || tex->rgba.empty()) return false;
            outView.rgba = tex->rgba.data();
            outView.width = tex->width;
            outView.height = tex->height;
            return true;
        };
    std::unordered_map<std::string, vfx::runtime::authored_batches::MeshData> tackleMeshesByPath;
    tackleMeshesByPath.reserve(tackleSnapshot.drawPasses.size());
    const auto resolveTackleMesh =
        [&](const std::string& modelPath) -> vfx::runtime::authored_batches::MeshData* {
            auto found = tackleMeshesByPath.find(modelPath);
            if (found != tackleMeshesByPath.end()) {
                return &found->second;
            }

            runtime::render_model::MeshData* mesh = args.ensureBackendMeshLoaded(modelPath);
            if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
                return nullptr;
            }

            auto inserted = tackleMeshesByPath.emplace(
                modelPath,
                game::runtime::shared_authored_vfx_interop::toReusableMeshData(*mesh));
            return &inserted.first->second;
        };

    std::vector<vfx::runtime::authored_batches::WorldIndexedBatch> tackleBatches;
    tackleBatches.reserve(tackleSnapshot.drawPasses.size() *
                          std::max<std::size_t>(tackleSnapshot.rings.size(), 1u) *
                          4u);

    // Gameplay can have multiple active Tackle hits overlapping. Splitting the snapshot
    // by active ring keeps each hit on the same single-ring authored path used by VfxLab,
    // rather than collapsing several hits into one pass batch with shared sort/blend state.
    SharedAuthoredBatchVFX::RenderSnapshot singleRingSnapshot = tackleSnapshot;
    singleRingSnapshot.rings.clear();
    singleRingSnapshot.rings.reserve(1u);
    for (const auto& ring : tackleSnapshot.rings) {
        singleRingSnapshot.rings.clear();
        singleRingSnapshot.rings.push_back(ring);
        vfx::runtime::authored_bridge::appendBatches(
            singleRingSnapshot,
            tackleBatches,
            args.cameraWorldPos,
            resolveTackleMesh,
            resolveTackleTextureView);
    }
    game::runtime::shared_authored_vfx_interop::appendWorldIndexedBatches(
        tackleBatches,
        *args.worldIndexedBatches);
}

void appendSharedTackleSmokeVfxSession(
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    GameWorld* gameWorld,
    const glm::vec3& cameraWorldPos,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded) {
    TackleSmokeVfxArgs args{};
    args.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
    args.hasWorldViewProj = hasWorldViewProj;
    args.gameWorld = gameWorld;
    args.cameraWorldPos = cameraWorldPos;
    args.backendTextureByPath = &backendTextureByPath;
    args.worldIndexedBatches = &worldIndexedBatches;
    args.ensureBackendMeshLoaded = ensureBackendMeshLoaded;
    args.ensureBackendTextureLoaded = ensureBackendTextureLoaded;
    appendSharedTackleSmokeVfx(args);
}

} // namespace game::runtime::shared_projected_scene
