#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"

#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxInterop.h"
#include "vfx/runtime/shared/SharedAuthoredVfxBridge.h"
#include "vfx/runtime/shared/SharedAuthoredVfxHelpers.h"

#include <unordered_map>

namespace game::runtime::shared_projected_scene {

void appendSharedGrowlWaveVfx(const GrowlWaveVfxArgs& args) {
    if (!args.useLegacyGrowlWaveVfx) return;
    if (!args.supportsWorldIndexedMeshes || !args.hasWorldViewProj) return;
    if (!args.gameWorld || !args.worldIndexedBatches || !args.backendTextureByPath) return;
    if (!args.ensureBackendMeshLoaded || !args.ensureBackendTextureLoaded) return;
    if (args.gameWorld->countActiveGrowlWaveVfx() == 0u) return;

    GrowlWaveVFX::RenderSnapshot growlSnapshot;
    if (!args.gameWorld->buildGrowlWaveSnapshot(growlSnapshot)) return;
    if (growlSnapshot.drawPasses.empty() || growlSnapshot.rings.empty()) return;

    using AuthoredTevState = vfx::runtime::authored::TevState;

    const auto resolveGrowlSharedTexture =
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const AuthoredTevState& tev) -> SharedBackendTextureCacheEntry* {
            if (vfx::runtime::authored::isLinePass(growlSnapshot.config, pass) ||
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
                vfx::runtime::authored::isQuarterRingPass(growlSnapshot.config, pass);
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
    const auto resolveGrowlTextureView =
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const AuthoredTevState& tev,
            vfx::runtime::authored_batches::TextureView& outView) {
            SharedBackendTextureCacheEntry* tex = resolveGrowlSharedTexture(pass, tev);
            if (!tex) tex = args.ensureBackendTextureLoaded("", false);
            if (!tex || !tex->valid || tex->rgba.empty()) return false;
            outView.rgba = tex->rgba.data();
            outView.width = tex->width;
            outView.height = tex->height;
            return true;
        };
    std::unordered_map<std::string, vfx::runtime::authored_batches::MeshData> growlMeshesByPath;
    growlMeshesByPath.reserve(growlSnapshot.drawPasses.size());
    const auto resolveGrowlMesh =
        [&](const std::string& modelPath) -> vfx::runtime::authored_batches::MeshData* {
            auto found = growlMeshesByPath.find(modelPath);
            if (found != growlMeshesByPath.end()) {
                return &found->second;
            }

            runtime::render_model::MeshData* mesh = args.ensureBackendMeshLoaded(modelPath);
            if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
                return nullptr;
            }

            auto inserted = growlMeshesByPath.emplace(
                modelPath,
                game::runtime::shared_authored_vfx_interop::toReusableMeshData(*mesh));
            return &inserted.first->second;
        };
    std::vector<vfx::runtime::authored_batches::WorldIndexedBatch> growlBatches;
    growlBatches.reserve(growlSnapshot.drawPasses.size() * 4u);
    vfx::runtime::authored_bridge::appendBatches(
        growlSnapshot,
        growlBatches,
        args.cameraWorldPos,
        resolveGrowlMesh,
        resolveGrowlTextureView);
    game::runtime::shared_authored_vfx_interop::appendWorldIndexedBatches(
        growlBatches,
        *args.worldIndexedBatches);
}

void appendSharedGrowlWaveVfxSession(
    bool useLegacyGrowlWaveVfx,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    GameWorld* gameWorld,
    const glm::vec3& cameraWorldPos,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded) {
    GrowlWaveVfxArgs args{};
    args.useLegacyGrowlWaveVfx = useLegacyGrowlWaveVfx;
    args.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
    args.hasWorldViewProj = hasWorldViewProj;
    args.gameWorld = gameWorld;
    args.cameraWorldPos = cameraWorldPos;
    args.backendTextureByPath = &backendTextureByPath;
    args.worldIndexedBatches = &worldIndexedBatches;
    args.ensureBackendMeshLoaded = ensureBackendMeshLoaded;
    args.ensureBackendTextureLoaded = ensureBackendTextureLoaded;
    appendSharedGrowlWaveVfx(args);
}

} // namespace game::runtime::shared_projected_scene

