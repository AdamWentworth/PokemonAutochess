#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"

#include "game/runtime/shared/vfx/growl/SharedGrowlInterop.h"
#include "vfx/runtime/growl/SharedGrowlVfxHelpers.h"
#include "vfx/runtime/growl/SharedGrowlWaveBridge.h"

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

    using GrowlTevState = vfx::runtime::growl::TevState;

    const auto resolveTackleSharedTexture =
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const GrowlTevState& tev) -> SharedBackendTextureCacheEntry* {
            if (vfx::runtime::growl::isLinePass(tackleSnapshot.config, pass) ||
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
                vfx::runtime::growl::isQuarterRingPass(tackleSnapshot.config, pass);
            auto& backendTextureByPath = *args.backendTextureByPath;
            if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
            const std::string bakedKey =
                vfx::runtime::growl::makeBakedTextureKey(pass, quarterPass);
            auto& baked = backendTextureByPath[bakedKey];
            if (baked.attemptedLoad) {
                return baked.valid ? &baked : nullptr;
            }

            baked.attemptedLoad = true;
            baked.valid = false;
            baked.width = rawTex->width;
            baked.height = rawTex->height;
            baked.rgba.clear();
            if (!vfx::runtime::growl::bakePassTextureRgba(
                    pass, tev, quarterPass, rawTex->rgba, baked.rgba)) {
                return nullptr;
            }

            baked.valid = true;
            return &baked;
        };
    const auto resolveTackleTextureView =
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const GrowlTevState& tev,
            vfx::runtime::growl_batches::TextureView& outView) {
            SharedBackendTextureCacheEntry* tex = resolveTackleSharedTexture(pass, tev);
            if (!tex) tex = args.ensureBackendTextureLoaded("", false);
            if (!tex || !tex->valid || tex->rgba.empty()) return false;
            outView.rgba = tex->rgba.data();
            outView.width = tex->width;
            outView.height = tex->height;
            return true;
        };
    std::unordered_map<std::string, vfx::runtime::growl_batches::MeshData> tackleMeshesByPath;
    tackleMeshesByPath.reserve(tackleSnapshot.drawPasses.size());
    const auto resolveTackleMesh =
        [&](const std::string& modelPath) -> vfx::runtime::growl_batches::MeshData* {
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
                game::runtime::shared_growl_interop::toReusableMeshData(*mesh));
            return &inserted.first->second;
        };

    std::vector<vfx::runtime::growl_batches::WorldIndexedBatch> tackleBatches;
    tackleBatches.reserve(tackleSnapshot.drawPasses.size() * 4u);
    vfx::runtime::growl_bridge::appendBatches(
        tackleSnapshot,
        tackleBatches,
        args.cameraWorldPos,
        resolveTackleMesh,
        resolveTackleTextureView);
    game::runtime::shared_growl_interop::appendWorldIndexedBatches(
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
