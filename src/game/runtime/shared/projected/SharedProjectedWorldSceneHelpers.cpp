#include "game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.h"

#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlVfxHelpers.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBridge.h"
#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"
#include "game/runtime/shared/vfx/particles/SharedParticleVfxBridgeDispatch.h"
#include "game/vfx/TailFireVFX.h"
#include "game/vfx/TailFireVFXConfigDB.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace game::runtime::shared_projected_scene {

ModelDepthBuffers acquireModelDepthBuffers(std::size_t reserveCount) {
    static thread_local std::vector<DepthTri> modelDepthTris;
    static thread_local std::vector<DepthWorldTri> modelDepthWorldTris;
    modelDepthTris.clear();
    modelDepthWorldTris.clear();
    if (modelDepthTris.capacity() < reserveCount) modelDepthTris.reserve(reserveCount);
    if (modelDepthWorldTris.capacity() < reserveCount) modelDepthWorldTris.reserve(reserveCount);
    return {modelDepthTris, modelDepthWorldTris};
}

shared_board_grid::Config makeBoardGridConfig(bool supportsWorldTriangles3D,
                                              int rows,
                                              int cols,
                                              int benchSlots,
                                              float worldCellSize,
                                              float boardMinX,
                                              float boardMinZ,
                                              float boardMaxX,
                                              float boardMaxZ,
                                              float boardX,
                                              float boardY,
                                              float boardW,
                                              float boardH,
                                              float cellW,
                                              float cellH,
                                              float line) {
    shared_board_grid::Config cfg;
    cfg.supportsWorldTriangles3D = supportsWorldTriangles3D;
    cfg.rows = rows;
    cfg.cols = cols;
    cfg.benchSlots = benchSlots;
    cfg.worldCellSize = worldCellSize;
    cfg.boardMinX = boardMinX;
    cfg.boardMinZ = boardMinZ;
    cfg.boardMaxX = boardMaxX;
    cfg.boardMaxZ = boardMaxZ;
    cfg.boardX = boardX;
    cfg.boardY = boardY;
    cfg.boardW = boardW;
    cfg.boardH = boardH;
    cfg.cellW = cellW;
    cfg.cellH = cellH;
    cfg.line = line;
    return cfg;
}

void flushModelDepthBuffers(std::vector<DepthTri>& modelDepthTris,
                            std::vector<DepthWorldTri>& modelDepthWorldTris,
                            std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
                            std::vector<IRenderBackend::WorldTriangle>& world3DTriangles) {
    if (!modelDepthWorldTris.empty()) {
        std::sort(
            modelDepthWorldTris.begin(),
            modelDepthWorldTris.end(),
            [](const DepthWorldTri& lhs, const DepthWorldTri& rhs) { return lhs.depth > rhs.depth; });
        world3DTriangles.reserve(world3DTriangles.size() + modelDepthWorldTris.size());
        for (const DepthWorldTri& tri : modelDepthWorldTris) {
            world3DTriangles.push_back(tri.tri);
        }
    }
    if (!modelDepthTris.empty()) {
        std::sort(
            modelDepthTris.begin(),
            modelDepthTris.end(),
            [](const DepthTri& lhs, const DepthTri& rhs) { return lhs.depth > rhs.depth; });
        worldTriangles.reserve(worldTriangles.size() + modelDepthTris.size());
        for (const DepthTri& tri : modelDepthTris) {
            worldTriangles.push_back(tri.tri);
        }
    }
}

void appendBoardAndBench(const shared_board_grid::Config& cfg,
                         std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
                         std::vector<IRenderBackend::WorldTriangle>& world3DTriangles,
                         std::vector<IRenderBackend::DebugQuad>& worldBackgroundQuads,
                         std::vector<IRenderBackend::DebugLine>& lines,
                         shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug) {
    shared_board_grid::appendBoardAndBench(
        cfg,
        worldTriangles,
        world3DTriangles,
        worldBackgroundQuads,
        lines,
        [&](const glm::vec3& a,
            const glm::vec3& b,
            const glm::vec3& c,
            const glm::vec3& d,
            float r,
            float g,
            float bl,
            float alpha) { projectedDebug.appendWorldQuad(a, b, c, d, r, g, bl, alpha); },
        [&](const glm::vec3& a,
            const glm::vec3& b,
            const glm::vec3& c,
            const glm::vec3& d,
            float r,
            float g,
            float bl,
            float alpha) { projectedDebug.appendProjectedQuad(a, b, c, d, r, g, bl, alpha); },
        [&](const glm::vec3& a,
            const glm::vec3& b,
            float r,
            float g,
            float bl,
            float alpha,
            float thickness) { projectedDebug.appendProjectedLine(a, b, r, g, bl, alpha, thickness); });
}

const TailFireVFX::Config& getTailFireFallbackCfg() {
    static TailFireVFX::Config sTailFireFallbackCfg{};
    static bool sTailFireFallbackCfgLoaded = false;
    if (!sTailFireFallbackCfgLoaded) {
        TailFireVFX::Config cfg;
        TailFireVFXConfigDB::get().ensureLoaded();
        TailFireVFXConfigDB::get().applyIfAny("charmander", cfg);
        sTailFireFallbackCfg = cfg;
        sTailFireFallbackCfgLoaded = true;
    }
    return sTailFireFallbackCfg;
}

const runtime::backend_model::MeshData* resolveModelMesh(
    const PokemonInstance& unit,
    const ::GameDataDb& dataDb,
    const std::function<runtime::backend_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded) {
    const PokemonStats* stats = dataDb.pokemon.getStats(unit.name);
    if (!stats || stats->model.empty()) return nullptr;

    const std::string modelPath = "assets/models/" + stats->model;
    runtime::backend_model::MeshData* mesh = ensureBackendMeshLoaded(modelPath);
    if (!mesh || mesh->indices.size() < 3u) {
        return nullptr;
    }
    return mesh;
}

void appendSharedGrowlWaveVfx(const GrowlWaveVfxArgs& args) {
    if (!args.useLegacyGrowlWaveVfx) return;
    if (!args.supportsWorldIndexedMeshes || !args.hasWorldViewProj) return;
    if (!args.gameWorld || !args.worldIndexedBatches || !args.backendTextureByPath) return;
    if (!args.ensureBackendMeshLoaded || !args.ensureBackendTextureLoaded) return;

    GrowlWaveVFX::RenderSnapshot growlSnapshot;
    if (!args.gameWorld->buildGrowlWaveSnapshot(growlSnapshot)) return;
    if (growlSnapshot.drawPasses.empty() || growlSnapshot.rings.empty()) return;

    using GrowlTevState = game::runtime::shared_growl::TevState;

    const auto resolveGrowlSharedTexture =
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const GrowlTevState& tev) -> SharedBackendTextureCacheEntry* {
            if (game::runtime::shared_growl::isLinePass(growlSnapshot.config, pass) ||
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
                game::runtime::shared_growl::isQuarterRingPass(growlSnapshot.config, pass);
            auto& backendTextureByPath = *args.backendTextureByPath;
            if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
            const std::string bakedKey =
                game::runtime::shared_growl::makeBakedTextureKey(pass, quarterPass);
            auto& baked = backendTextureByPath[bakedKey];
            if (baked.attemptedLoad) {
                return baked.valid ? &baked : nullptr;
            }

            baked.attemptedLoad = true;
            baked.valid = false;
            baked.width = rawTex->width;
            baked.height = rawTex->height;
            baked.rgba.clear();
            if (!game::runtime::shared_growl::bakePassTextureRgba(
                    pass, tev, quarterPass, rawTex->rgba, baked.rgba)) {
                return nullptr;
            }

            baked.valid = true;
            return &baked;
        };
    const auto resolveGrowlTextureView =
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const GrowlTevState& tev,
            game::runtime::shared_growl_batches::TextureView& outView) {
            SharedBackendTextureCacheEntry* tex = resolveGrowlSharedTexture(pass, tev);
            if (!tex) tex = args.ensureBackendTextureLoaded("", false);
            if (!tex || !tex->valid || tex->rgba.empty()) return false;
            outView.rgba = tex->rgba.data();
            outView.width = tex->width;
            outView.height = tex->height;
            return true;
        };

    game::runtime::shared_growl_bridge::appendBatches(
        growlSnapshot,
        *args.worldIndexedBatches,
        args.cameraWorldPos,
        args.ensureBackendMeshLoaded,
        resolveGrowlTextureView);
}

void appendSharedGrowlWaveVfxSession(
    bool useLegacyGrowlWaveVfx,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    GameWorld* gameWorld,
    const glm::vec3& cameraWorldPos,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    const std::function<runtime::backend_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
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

void appendSharedParticleVfx(const ParticleVfxArgs& args) {
    if (!args.useLegacyParticleVfxSnapshotBridge) return;
    if (!args.supportsWorldIndexedMeshes || !args.hasWorldViewProj) return;
    if (!args.gameWorld || !args.projectedDebug || !args.sharedTailFireAnchors ||
        !args.backendTextureByPath || !args.worldIndexedBatches) {
        return;
    }
    if (!args.ensureBackendTextureLoaded) return;

    GameWorld::ParticleVfxSnapshots vfxSnapshots;
    (void)args.gameWorld->buildParticleVfxSnapshots(vfxSnapshots);

    const auto appendSnapshotAsBillboards =
        [&](const char* label, const ParticleSystem::RenderSnapshot& snapshot) -> bool {
        return game::runtime::shared_particle_snapshot_billboards::appendSnapshotAsBillboards(
            label,
            snapshot,
            args.viewProj,
            args.invViewProj,
            args.cameraWorldPos,
            args.drawableW,
            args.drawableH,
            *args.backendTextureByPath,
            [&](const std::string& texturePath, bool flipVertical) -> SharedBackendTextureCacheEntry* {
                return args.ensureBackendTextureLoaded(texturePath, flipVertical);
            },
            args.useExactTailFireCpuPath,
            *args.worldIndexedBatches);
    };
    const auto particleDispatchResult =
        game::runtime::shared_particle_bridge_dispatch::appendStandardSnapshots(
            vfxSnapshots,
            appendSnapshotAsBillboards);
    bool appendedTailFireBillboards = particleDispatchResult.appendedTailFireBillboards;
    const bool appendedLeechDrainBillboards = particleDispatchResult.appendedLeechDrainBillboards;

    if (!appendedTailFireBillboards && args.gameWorld) {
        const TailFireVFX::Config& tailFireFallbackCfg = getTailFireFallbackCfg();
        game::runtime::shared_tail_fire_fallback::Args tailFireArgs;
        tailFireArgs.worldCellSize = args.worldCellSize;
        tailFireArgs.simNowSec = args.simNowSec;
        tailFireArgs.cfg = &tailFireFallbackCfg;
        tailFireArgs.anchors = args.sharedTailFireAnchors;
        tailFireArgs.pokemons = &args.gameWorld->getPokemons();
        tailFireArgs.benchPokemons = &args.gameWorld->getBenchPokemons();
        tailFireArgs.appendSnapshot = appendSnapshotAsBillboards;
        appendedTailFireBillboards =
            game::runtime::shared_tail_fire_fallback::appendSyntheticTailFire(tailFireArgs) ||
            appendedTailFireBillboards;
    }

    if (!appendedTailFireBillboards ||
        (!appendedLeechDrainBillboards && !args.useLegacyParticleVfxSnapshotBridge)) {
        for (const auto& unit : args.gameWorld->getPokemons()) {
            const auto extents =
                game::runtime::backend_proxy::computeUnitProxyExtents(unit, args.worldCellSize);
            const glm::vec3 proxyCenter =
                unit.position + glm::vec3(0.0f, unit.visualYOffset, 0.0f);

            if (!appendedTailFireBillboards) {
                args.projectedDebug->appendProjectedTailFire(
                    unit,
                    proxyCenter,
                    extents,
                    unit.rotation.y,
                    std::max(1.0f, args.lineThickness * 0.92f));
            }
            if (!appendedLeechDrainBillboards && !args.useLegacyParticleVfxSnapshotBridge) {
                args.projectedDebug->appendProjectedLeechDrain(
                    args.gameWorld,
                    unit,
                    std::max(0.12f, args.worldCellSize * 0.24f),
                    std::max(1.0f, args.lineThickness));
            }
        }
    }
}

void appendSharedParticleVfxSession(
    bool useLegacyParticleVfxSnapshotBridge,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    bool useExactTailFireCpuPath,
    GameWorld* gameWorld,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    float worldCellSize,
    double simNowSec,
    float lineThickness,
    std::unordered_map<int, shared_tail_fire_fallback::Anchor>& sharedTailFireAnchors,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded) {
    ParticleVfxArgs args{};
    args.useLegacyParticleVfxSnapshotBridge = useLegacyParticleVfxSnapshotBridge;
    args.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
    args.hasWorldViewProj = hasWorldViewProj;
    args.useExactTailFireCpuPath = useExactTailFireCpuPath;
    args.gameWorld = gameWorld;
    args.viewProj = viewProj;
    args.invViewProj = invViewProj;
    args.cameraWorldPos = cameraWorldPos;
    args.drawableW = drawableW;
    args.drawableH = drawableH;
    args.worldCellSize = worldCellSize;
    args.simNowSec = simNowSec;
    args.lineThickness = lineThickness;
    args.sharedTailFireAnchors = &sharedTailFireAnchors;
    args.backendTextureByPath = &backendTextureByPath;
    args.worldIndexedBatches = &worldIndexedBatches;
    args.projectedDebug = &projectedDebug;
    args.ensureBackendTextureLoaded = ensureBackendTextureLoaded;
    appendSharedParticleVfx(args);
}

void appendSharedProjectedVfxBridgesSession(
    bool useLegacyParticleVfxSnapshotBridge,
    bool useLegacyGrowlWaveVfx,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    bool useExactTailFireCpuPath,
    GameWorld* gameWorld,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    float worldCellSize,
    double simNowSec,
    float lineThickness,
    std::unordered_map<int, shared_tail_fire_fallback::Anchor>& sharedTailFireAnchors,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
    const std::function<runtime::backend_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded) {
    appendSharedParticleVfxSession(
        useLegacyParticleVfxSnapshotBridge,
        supportsWorldIndexedMeshes,
        hasWorldViewProj,
        useExactTailFireCpuPath,
        gameWorld,
        viewProj,
        invViewProj,
        cameraWorldPos,
        drawableW,
        drawableH,
        worldCellSize,
        simNowSec,
        lineThickness,
        sharedTailFireAnchors,
        backendTextureByPath,
        worldIndexedBatches,
        projectedDebug,
        ensureBackendTextureLoaded);
    appendSharedGrowlWaveVfxSession(
        useLegacyGrowlWaveVfx,
        supportsWorldIndexedMeshes,
        hasWorldViewProj,
        gameWorld,
        cameraWorldPos,
        backendTextureByPath,
        worldIndexedBatches,
        ensureBackendMeshLoaded,
        ensureBackendTextureLoaded);
}

runtime::shared_capture::SnapshotCache makeSharedCaptureSnapshotCache(std::size_t reserveCount) {
    runtime::shared_capture::SnapshotCache cache;
    cache.snaps.reserve(reserveCount);
    cache.byTargetId.reserve(reserveCount);
    return cache;
}

bool appendSharedCaptureAttemptModels(const CaptureModelBridgeArgs& args) {
    shared_capture_model_bridge::Args bridgeArgs{};
    bridgeArgs.gameWorld = args.gameWorld;
    bridgeArgs.supportsWorldIndexedMeshes = args.supportsWorldIndexedMeshes;
    bridgeArgs.hasWorldViewProj = args.hasWorldViewProj;
    bridgeArgs.drawableW = args.drawableW;
    bridgeArgs.drawableH = args.drawableH;
    bridgeArgs.worldCellSize = args.worldCellSize;
    bridgeArgs.worldViewProj = args.worldViewProj;
    bridgeArgs.cameraWorldPos = args.cameraWorldPos;
    bridgeArgs.sharedCaptureAttemptCache = args.sharedCaptureAttemptCache;
    bridgeArgs.renderer = args.renderer;
    bridgeArgs.worldIndexedBatches = args.worldIndexedBatches;
    bridgeArgs.backendTextureByPath = args.backendTextureByPath;
    bridgeArgs.ensureBackendMeshLoaded = args.ensureBackendMeshLoaded;
    bridgeArgs.ensureBackendTextureLoaded = args.ensureBackendTextureLoaded;
    bridgeArgs.evaluateScenePoseForClipTime =
        [](const runtime::backend_model::MeshData& mesh, int animIndex, float animTimeSec) {
            return game::runtime::shared_backend_pose::evaluateScenePoseForClipTime(
                mesh, animIndex, animTimeSec);
        };
    return shared_capture_model_bridge::appendSharedCaptureAttemptModels(bridgeArgs);
}

bool appendSharedCaptureAttemptModelsSession(
    GameWorld* gameWorld,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    int drawableW,
    int drawableH,
    float worldCellSize,
    const float* worldViewProj,
    const glm::vec3& cameraWorldPos,
    runtime::shared_capture::SnapshotCache& sharedCaptureAttemptCache,
    IRenderBackend* renderer,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    const std::function<runtime::backend_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&)>& ensureBackendTextureLoaded) {
    CaptureModelBridgeArgs args{};
    args.gameWorld = gameWorld;
    args.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
    args.hasWorldViewProj = hasWorldViewProj;
    args.drawableW = drawableW;
    args.drawableH = drawableH;
    args.worldCellSize = worldCellSize;
    args.worldViewProj = worldViewProj;
    args.cameraWorldPos = cameraWorldPos;
    args.sharedCaptureAttemptCache = &sharedCaptureAttemptCache;
    args.renderer = renderer;
    args.worldIndexedBatches = &worldIndexedBatches;
    args.backendTextureByPath = &backendTextureByPath;
    args.ensureBackendMeshLoaded = ensureBackendMeshLoaded;
    args.ensureBackendTextureLoaded = ensureBackendTextureLoaded;
    return appendSharedCaptureAttemptModels(args);
}

bool appendSharedCaptureAttemptModelsIfNeededForProjectedWorld(
    IRenderBackend* renderer,
    GameWorld* gameWorld,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    int drawableW,
    int drawableH,
    float worldCellSize,
    const float* worldViewProj,
    const glm::vec3& cameraWorldPos,
    runtime::shared_capture::SnapshotCache& sharedCaptureAttemptCache,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    const std::function<runtime::backend_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&)>& ensureBackendTextureLoaded) {
    const char* backendId = (renderer ? renderer->backendId() : nullptr);
    if (backendId != nullptr) {
        std::string id(backendId);
        std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (id == "opengl") {
            return false;
        }
    }
    return appendSharedCaptureAttemptModelsSession(
        gameWorld,
        supportsWorldIndexedMeshes,
        hasWorldViewProj,
        drawableW,
        drawableH,
        worldCellSize,
        worldViewProj,
        cameraWorldPos,
        sharedCaptureAttemptCache,
        renderer,
        worldIndexedBatches,
        backendTextureByPath,
        ensureBackendMeshLoaded,
        ensureBackendTextureLoaded);
}

} // namespace game::runtime::shared_projected_scene
