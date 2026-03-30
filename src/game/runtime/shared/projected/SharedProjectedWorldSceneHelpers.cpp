#include "game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.h"

#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "vfx/runtime/growl/SharedGrowlVfxHelpers.h"
#include "vfx/runtime/growl/SharedGrowlWaveBridge.h"
#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"
#include "game/runtime/shared/vfx/particles/SharedParticleVfxBridgeDispatch.h"
#include "game/vfx/TailFireVFXConfig.h"
#include "game/vfx/TailFireVFXConfigDB.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <iostream>
#include <utility>

namespace game::runtime::shared_projected_scene {
namespace {

struct BoardBenchGeometryCacheKey {
    bool supportsWorldTriangles3D = false;
    int rows = 0;
    int cols = 0;
    int benchSlots = 0;
    float worldCellSize = 0.0f;
    float boardMinX = 0.0f;
    float boardMinZ = 0.0f;
    float boardMaxX = 0.0f;
    float boardMaxZ = 0.0f;
    float boardX = 0.0f;
    float boardY = 0.0f;
    float boardW = 0.0f;
    float boardH = 0.0f;
    float cellW = 0.0f;
    float cellH = 0.0f;
    float line = 0.0f;
    const shared_board_grid::VisualTheme* visualTheme = nullptr;

    bool operator==(const BoardBenchGeometryCacheKey& other) const {
        return supportsWorldTriangles3D == other.supportsWorldTriangles3D &&
               rows == other.rows &&
               cols == other.cols &&
               benchSlots == other.benchSlots &&
               worldCellSize == other.worldCellSize &&
               boardMinX == other.boardMinX &&
               boardMinZ == other.boardMinZ &&
               boardMaxX == other.boardMaxX &&
               boardMaxZ == other.boardMaxZ &&
               boardX == other.boardX &&
               boardY == other.boardY &&
               boardW == other.boardW &&
               boardH == other.boardH &&
               cellW == other.cellW &&
               cellH == other.cellH &&
               line == other.line &&
               visualTheme == other.visualTheme;
    }
};

struct BoardBenchGeometryCache {
    bool valid = false;
    BoardBenchGeometryCacheKey key{};
    std::vector<IRenderBackend::DebugTriangle> worldTriangles;
    std::vector<IRenderBackend::WorldTriangle> world3DTriangles;
    std::vector<IRenderBackend::DebugQuad> worldBackgroundQuads;
    std::vector<IRenderBackend::DebugLine> lines;
};

BoardBenchGeometryCacheKey makeBoardBenchGeometryCacheKey(const shared_board_grid::Config& cfg) {
    BoardBenchGeometryCacheKey key{};
    key.supportsWorldTriangles3D = cfg.supportsWorldTriangles3D;
    key.rows = cfg.rows;
    key.cols = cfg.cols;
    key.benchSlots = cfg.benchSlots;
    key.worldCellSize = cfg.worldCellSize;
    key.boardMinX = cfg.boardMinX;
    key.boardMinZ = cfg.boardMinZ;
    key.boardMaxX = cfg.boardMaxX;
    key.boardMaxZ = cfg.boardMaxZ;
    key.boardX = cfg.boardX;
    key.boardY = cfg.boardY;
    key.boardW = cfg.boardW;
    key.boardH = cfg.boardH;
    key.cellW = cfg.cellW;
    key.cellH = cfg.cellH;
    key.line = cfg.line;
    key.visualTheme = cfg.visualTheme ? cfg.visualTheme : &shared_board_grid::defaultVisualTheme();
    return key;
}

void appendWorldQuadToTriangles(std::vector<IRenderBackend::WorldTriangle>& out,
                                const glm::vec3& a,
                                const glm::vec3& b,
                                const glm::vec3& c,
                                const glm::vec3& d,
                                float r,
                                float g,
                                float bl,
                                float alpha) {
    IRenderBackend::WorldTriangle t0{};
    t0.x1 = a.x; t0.y1 = a.y; t0.z1 = a.z;
    t0.x2 = b.x; t0.y2 = b.y; t0.z2 = b.z;
    t0.x3 = c.x; t0.y3 = c.y; t0.z3 = c.z;
    t0.r = r; t0.g = g; t0.b = bl; t0.a = alpha;
    out.push_back(t0);

    IRenderBackend::WorldTriangle t1{};
    t1.x1 = a.x; t1.y1 = a.y; t1.z1 = a.z;
    t1.x2 = c.x; t1.y2 = c.y; t1.z2 = c.z;
    t1.x3 = d.x; t1.y3 = d.y; t1.z3 = d.z;
    t1.r = r; t1.g = g; t1.b = bl; t1.a = alpha;
    out.push_back(t1);
}

void appendCachedBoardAndBench3D(const shared_board_grid::Config& cfg,
                                 std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
                                 std::vector<IRenderBackend::WorldTriangle>& world3DTriangles,
                                 std::vector<IRenderBackend::DebugQuad>& worldBackgroundQuads,
                                 std::vector<IRenderBackend::DebugLine>& lines) {
    static thread_local BoardBenchGeometryCache cache;
    const BoardBenchGeometryCacheKey key = makeBoardBenchGeometryCacheKey(cfg);
    if (!cache.valid || !(cache.key == key)) {
        cache.valid = true;
        cache.key = key;
        cache.worldTriangles.clear();
        cache.world3DTriangles.clear();
        cache.worldBackgroundQuads.clear();
        cache.lines.clear();

        shared_board_grid::appendBoardAndBench(
            cfg,
            cache.worldTriangles,
            cache.world3DTriangles,
            cache.worldBackgroundQuads,
            cache.lines,
            [&](const glm::vec3& a,
                const glm::vec3& b,
                const glm::vec3& c,
                const glm::vec3& d,
                float r,
                float g,
                float bl,
                float alpha) {
                appendWorldQuadToTriangles(cache.world3DTriangles, a, b, c, d, r, g, bl, alpha);
            },
            [&](const glm::vec3&,
                const glm::vec3&,
                const glm::vec3&,
                const glm::vec3&,
                float,
                float,
                float,
                float) {},
            [&](const glm::vec3&,
                const glm::vec3&,
                float,
                float,
                float,
                float,
                float) {});
    }

    if (!cache.worldTriangles.empty()) {
        worldTriangles.reserve(worldTriangles.size() + cache.worldTriangles.size());
        worldTriangles.insert(
            worldTriangles.end(),
            cache.worldTriangles.begin(),
            cache.worldTriangles.end());
    }
    if (!cache.world3DTriangles.empty()) {
        world3DTriangles.reserve(world3DTriangles.size() + cache.world3DTriangles.size());
        world3DTriangles.insert(
            world3DTriangles.end(),
            cache.world3DTriangles.begin(),
            cache.world3DTriangles.end());
    }
    if (!cache.worldBackgroundQuads.empty()) {
        worldBackgroundQuads.reserve(worldBackgroundQuads.size() + cache.worldBackgroundQuads.size());
        worldBackgroundQuads.insert(
            worldBackgroundQuads.end(),
            cache.worldBackgroundQuads.begin(),
            cache.worldBackgroundQuads.end());
    }
    if (!cache.lines.empty()) {
        lines.reserve(lines.size() + cache.lines.size());
        lines.insert(lines.end(), cache.lines.begin(), cache.lines.end());
    }
}

bool isCharmanderUnit(const PokemonInstance& unit) {
    const std::string species = unit.name;
    if (species.size() != 10u) return false;
    return std::tolower(static_cast<unsigned char>(species[0])) == 'c' &&
           std::tolower(static_cast<unsigned char>(species[1])) == 'h' &&
           std::tolower(static_cast<unsigned char>(species[2])) == 'a' &&
           std::tolower(static_cast<unsigned char>(species[3])) == 'r' &&
           std::tolower(static_cast<unsigned char>(species[4])) == 'm' &&
           std::tolower(static_cast<unsigned char>(species[5])) == 'a' &&
           std::tolower(static_cast<unsigned char>(species[6])) == 'n' &&
           std::tolower(static_cast<unsigned char>(species[7])) == 'd' &&
           std::tolower(static_cast<unsigned char>(species[8])) == 'e' &&
           std::tolower(static_cast<unsigned char>(species[9])) == 'r';
}

} // namespace

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
    cfg.visualTheme = &shared_board_grid::defaultVisualTheme();
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
    if (cfg.supportsWorldTriangles3D) {
        appendCachedBoardAndBench3D(
            cfg,
            worldTriangles,
            world3DTriangles,
            worldBackgroundQuads,
            lines);
        return;
    }

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

const TailFireVFXConfig& getTailFireFallbackCfg() {
    static TailFireVFXConfig sTailFireFallbackCfg{};
    static bool sTailFireFallbackCfgLoaded = false;
    if (!sTailFireFallbackCfgLoaded) {
        const auto start = std::chrono::steady_clock::now();
        TailFireVFXConfig cfg;
        TailFireVFXConfigDB::get().ensureLoaded();
        TailFireVFXConfigDB::get().applyIfAny("charmander", cfg);
        sTailFireFallbackCfg = cfg;
        sTailFireFallbackCfgLoaded = true;

        const auto end = std::chrono::steady_clock::now();
        const double totalMs =
            std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << "[TailFire][CPU] fallback_config species=charmander total="
                  << totalMs
                  << "ms flipbook0="
                  << sTailFireFallbackCfg.flipbookPath
                  << " flipbook1="
                  << (sTailFireFallbackCfg.useFlipbook2
                          ? sTailFireFallbackCfg.flipbook2Path
                          : std::string("<disabled>"))
                  << "\n";
    }
    return sTailFireFallbackCfg;
}

bool hasValidTailFireAnchor(
    const std::unordered_map<int, shared_tail_fire_fallback::Anchor>& anchors) {
    for (const auto& [unitId, anchor] : anchors) {
        (void)unitId;
        if (anchor.valid && !anchor.meshCarrierActive) return true;
    }
    return false;
}

bool hasMeshCarrierTailFire(
    const std::unordered_map<int, shared_tail_fire_fallback::Anchor>& anchors) {
    for (const auto& [unitId, anchor] : anchors) {
        (void)unitId;
        if (anchor.valid && anchor.meshCarrierActive) return true;
    }
    return false;
}

bool appendAnchoredSingleFlipbookTailFire(const ParticleVfxArgs& args,
                                          const TailFireVFXConfig& cfg) {
    if (!args.sharedTailFireAnchors || !args.backendTextureByPath ||
        !args.worldIndexedBatches || !args.ensureBackendTextureLoaded) {
        return false;
    }
    if (!cfg.useFlipbook || cfg.flipbookPath.empty() || cfg.useFlipbook2) {
        return false;
    }
    if (!hasValidTailFireAnchor(*args.sharedTailFireAnchors)) {
        return false;
    }

    ParticleSystem::RenderSnapshot snapshot{};
    snapshot.renderSettings.blend = cfg.blend;
    snapshot.renderSettings.depthTest = cfg.depthTest;
    snapshot.renderSettings.depthWrite = cfg.depthWrite;
    snapshot.pointScale = cfg.pointScale;
    snapshot.timeSec = static_cast<float>(args.simNowSec);
    snapshot.shaderVertPath = cfg.vertShaderPath;
    snapshot.shaderFragPath = cfg.fragShaderPath;
    snapshot.useFlipbook = true;
    snapshot.flipbookPath = cfg.flipbookPath;
    snapshot.flipbookCols = cfg.flipbookCols;
    snapshot.flipbookRows = cfg.flipbookRows;
    snapshot.flipbookFrames = cfg.flipbookFrames;
    snapshot.flipbookFps = cfg.flipbookFps;
    snapshot.useSecondaryFlipbook = false;

    ParticleSystem::Particle marker{};
    marker.lifeSec = 1.0f;
    marker.maxLifeSec = 1.0f;
    marker.sizePx = 0.30f;
    marker.seed = 0.0f;
    snapshot.particles.push_back(marker);

    return game::runtime::shared_particle_snapshot_billboards::appendSnapshotAsBillboards(
        "tail_fire_single",
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
        args.sharedTailFireAnchors,
        args.useExactTailFireCpuPath,
        *args.worldIndexedBatches);
}

const runtime::render_model::MeshData* resolveModelMesh(
    const PokemonInstance& unit,
    const ::GameDataDb& dataDb,
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded) {
    std::string modelPath = unit.backendModelPath;
    if (modelPath.empty()) {
        if (!unit.animIndexCacheSourceModelPath.empty()) {
            modelPath = unit.animIndexCacheSourceModelPath;
        } else if (!unit.backendAnimDurationsSourceModelPath.empty()) {
            modelPath = unit.backendAnimDurationsSourceModelPath;
        } else {
            const PokemonStats* stats = dataDb.pokemon.getStats(unit.name);
            if (!stats || stats->model.empty()) return nullptr;
            modelPath = "assets/models/" + stats->model;
        }
    }

    runtime::render_model::MeshData* mesh = ensureBackendMeshLoaded(modelPath);
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
    if (args.gameWorld->countActiveGrowlWaveVfx() == 0u) return;

    GrowlWaveVFX::RenderSnapshot growlSnapshot;
    if (!args.gameWorld->buildGrowlWaveSnapshot(growlSnapshot)) return;
    if (growlSnapshot.drawPasses.empty() || growlSnapshot.rings.empty()) return;

    using GrowlTevState = vfx::runtime::growl::TevState;

    const auto resolveGrowlSharedTexture =
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const GrowlTevState& tev) -> SharedBackendTextureCacheEntry* {
            if (vfx::runtime::growl::isLinePass(growlSnapshot.config, pass) ||
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
                vfx::runtime::growl::isQuarterRingPass(growlSnapshot.config, pass);
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
    const auto resolveGrowlTextureView =
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const GrowlTevState& tev,
            vfx::runtime::growl_batches::TextureView& outView) {
            SharedBackendTextureCacheEntry* tex = resolveGrowlSharedTexture(pass, tev);
            if (!tex) tex = args.ensureBackendTextureLoaded("", false);
            if (!tex || !tex->valid || tex->rgba.empty()) return false;
            outView.rgba = tex->rgba.data();
            outView.width = tex->width;
            outView.height = tex->height;
            return true;
        };

    vfx::runtime::growl_bridge::appendBatches(
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

void appendSharedParticleVfx(const ParticleVfxArgs& args) {
    if (!args.useLegacyParticleVfxSnapshotBridge) return;
    if (!args.supportsWorldIndexedMeshes || !args.hasWorldViewProj) return;
    if (!args.gameWorld || !args.projectedDebug || !args.sharedTailFireAnchors ||
        !args.backendTextureByPath || !args.worldIndexedBatches) {
        return;
    }
    if (!args.ensureBackendTextureLoaded) return;

    bool appendedTailFireBillboards = false;
    bool appendedLeechDrainBillboards = false;
    const TailFireVFXConfig& tailFireFallbackCfg = getTailFireFallbackCfg();
    const bool wantsAnchoredSingleFlipbook =
        tailFireFallbackCfg.useFlipbook &&
        !tailFireFallbackCfg.flipbookPath.empty() &&
        !tailFireFallbackCfg.useFlipbook2;
    if (wantsAnchoredSingleFlipbook) {
        appendedTailFireBillboards =
            appendAnchoredSingleFlipbookTailFire(args, tailFireFallbackCfg);
    }

    if (args.gameWorld->countActiveParticleVfx() > 0u) {
        GameWorld::ParticleVfxSnapshots vfxSnapshots;
        (void)args.gameWorld->buildParticleVfxSnapshots(vfxSnapshots);

        const auto appendSnapshotAsBillboards =
            [&](const char* label, const ParticleSystem::RenderSnapshot& snapshot) -> bool {
            if (wantsAnchoredSingleFlipbook &&
                appendedTailFireBillboards &&
                label != nullptr &&
                std::strcmp(label, "tail_fire") == 0) {
                return true;
            }
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
                args.sharedTailFireAnchors,
                args.useExactTailFireCpuPath,
                *args.worldIndexedBatches);
        };
        const auto particleDispatchResult =
            game::runtime::shared_particle_bridge_dispatch::appendStandardSnapshots(
                vfxSnapshots,
                appendSnapshotAsBillboards);
        appendedTailFireBillboards = particleDispatchResult.appendedTailFireBillboards;
        appendedLeechDrainBillboards = particleDispatchResult.appendedLeechDrainBillboards;
    }

    if (!appendedTailFireBillboards && args.gameWorld) {
        game::runtime::shared_tail_fire_fallback::Args tailFireArgs;
        tailFireArgs.worldCellSize = args.worldCellSize;
        tailFireArgs.simNowSec = args.simNowSec;
        tailFireArgs.cfg = &tailFireFallbackCfg;
        tailFireArgs.anchors = args.sharedTailFireAnchors;
        tailFireArgs.pokemons = &args.gameWorld->getPokemons();
        tailFireArgs.benchPokemons = &args.gameWorld->getBenchPokemons();
        tailFireArgs.appendSnapshot =
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
                    args.sharedTailFireAnchors,
                    args.useExactTailFireCpuPath,
                    *args.worldIndexedBatches);
            };
        appendedTailFireBillboards =
            game::runtime::shared_tail_fire_fallback::appendSyntheticTailFire(tailFireArgs) ||
            appendedTailFireBillboards;
    }

    if (!appendedTailFireBillboards &&
        !(args.sharedTailFireAnchors && hasMeshCarrierTailFire(*args.sharedTailFireAnchors))) {
        for (const auto& unit : args.gameWorld->getPokemons()) {
            if (!isCharmanderUnit(unit)) continue;
            const auto extents =
                game::runtime::render_prep_proxy::computeUnitProxyExtents(unit, args.worldCellSize);
            const glm::vec3 proxyCenter =
                unit.position + glm::vec3(0.0f, unit.visualYOffset, 0.0f);

            args.projectedDebug->appendProjectedTailFire(
                unit,
                proxyCenter,
                extents,
                unit.rotation.y,
                std::max(1.0f, args.lineThickness * 0.92f));
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
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
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
        [](const runtime::render_model::MeshData& mesh, int animIndex, float animTimeSec) {
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
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
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
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
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

