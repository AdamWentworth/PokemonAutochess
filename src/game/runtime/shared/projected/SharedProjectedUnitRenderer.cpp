#include "game/runtime/shared/projected/SharedProjectedUnitRenderer.h"

#include "game/runtime/render_prep/ProceduralPose.h"
#include "game/runtime/render_prep/MaterialShading.h"
#include "game/runtime/render_prep/UnitVisuals.h"
#include "game/runtime/render_prep/WorldProxyGeometry.h"
#include "game/runtime/shared/projected/SharedProjectedUnitModelRenderer.h"
#include "game/runtime/shared/projected/SharedProjectedUnitOverlays.h"
#include "game/world/MoveImpactRouting.h"
#include "engine/core/Environment.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {
constexpr float kSimulationFixedStepSec = 1.0f / 60.0f;

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::size_t selectUniformTriangleIndex(std::size_t sampleIndex,
                                       std::size_t sampleCount,
                                       std::size_t triangleCount) {
    if (triangleCount == 0u || sampleCount == 0u) return 0u;
    if (sampleCount >= triangleCount) return std::min(sampleIndex, triangleCount - 1u);
    const double t = (static_cast<double>(sampleIndex) + 0.5) /
                     static_cast<double>(sampleCount);
    const std::size_t idx =
        static_cast<std::size_t>(t * static_cast<double>(triangleCount));
    return std::min(idx, triangleCount - 1u);
}

bool backendClipSkinningAdaptiveEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_CLIP_SKINNING_ADAPTIVE");
        // Quality-first default: keep clip skinning enabled for all eligible units
        // unless adaptive throttling is explicitly requested.
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

std::size_t backendClipSkinningMaxUnits() {
    static const std::size_t maxUnits = []() -> std::size_t {
        // Large default so adaptive mode does not degrade small/normal battles.
        constexpr std::size_t kDefault = 64u;
        constexpr std::size_t kMin = 1u;
        constexpr std::size_t kMax = 256u;
        const auto env = engine::env::get("PAC_BACKEND_CLIP_SKINNING_MAX_UNITS");
        if (!env.has_value()) return kDefault;
        try {
            const std::size_t parsed = static_cast<std::size_t>(std::stoull(*env));
            return std::clamp(parsed, kMin, kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return maxUnits;
}

float backendScenePoseCacheHz() {
    static const float hz = []() -> float {
        constexpr float kDefault = 30.0f;
        constexpr float kMin = 0.0f;
        constexpr float kMax = 240.0f;
        const auto env = engine::env::get("PAC_BACKEND_SCENE_POSE_CACHE_HZ");
        if (!env.has_value()) return kDefault;
        try {
            return std::clamp(std::stof(*env), kMin, kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return hz;
}

float backendScenePoseCacheSparseHz() {
    static const float hz = []() -> float {
        // Align sparse-battle pose caching with the 60 Hz simulation step.
        // Higher defaults fragment the cache into intermediate buckets the scene
        // never requests because unit animTimeSec only advances on fixed ticks.
        constexpr float kDefault = 60.0f;
        constexpr float kMin = 0.0f;
        constexpr float kMax = 240.0f;
        const auto env = engine::env::get("PAC_BACKEND_SCENE_POSE_CACHE_SPARSE_HZ");
        if (!env.has_value()) return kDefault;
        try {
            return std::clamp(std::stof(*env), kMin, kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return hz;
}

float backendScenePoseCacheDenseHz() {
    static const float hz = []() -> float {
        constexpr float kDefault = 20.0f;
        constexpr float kMin = 0.0f;
        constexpr float kMax = 240.0f;
        const auto env = engine::env::get("PAC_BACKEND_SCENE_POSE_CACHE_DENSE_HZ");
        if (!env.has_value()) return kDefault;
        try {
            return std::clamp(std::stof(*env), kMin, kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return hz;
}

std::size_t backendScenePoseCacheMinUnits() {
    static const std::size_t minUnits = []() -> std::size_t {
        constexpr std::size_t kDefault = 8u;
        constexpr std::size_t kMin = 1u;
        constexpr std::size_t kMax = 128u;
        const auto env = engine::env::get("PAC_BACKEND_SCENE_POSE_CACHE_MIN_UNITS");
        if (!env.has_value()) return kDefault;
        try {
            const std::size_t parsed = static_cast<std::size_t>(std::stoull(*env));
            return std::clamp(parsed, kMin, kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return minUnits;
}

std::size_t backendScenePoseCacheDenseMinUnits() {
    static const std::size_t minUnits = []() -> std::size_t {
        constexpr std::size_t kDefault = 12u;
        constexpr std::size_t kMin = 1u;
        constexpr std::size_t kMax = 128u;
        const auto env = engine::env::get("PAC_BACKEND_SCENE_POSE_CACHE_DENSE_MIN_UNITS");
        if (!env.has_value()) return kDefault;
        try {
            const std::size_t parsed = static_cast<std::size_t>(std::stoull(*env));
            return std::clamp(parsed, kMin, kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return minUnits;
}

float backendScenePoseCacheEffectiveHz(std::size_t unitCount) {
    if (unitCount == 0u) {
        return 0.0f;
    }
    const std::size_t minUnits = backendScenePoseCacheMinUnits();
    if (unitCount < minUnits) {
        const float sparseHz = backendScenePoseCacheSparseHz();
        return (sparseHz > 0.0f) ? sparseHz : 0.0f;
    }
    const float baseHz = backendScenePoseCacheHz();
    if (!(baseHz > 0.0f)) {
        return 0.0f;
    }
    const float denseHz = backendScenePoseCacheDenseHz();
    if (unitCount >= backendScenePoseCacheDenseMinUnits() && denseHz > 0.0f) {
        return std::min(baseHz, denseHz);
    }
    return baseHz;
}

int resolveSceneAnimIndexForUnit(const game::runtime::render_model::MeshData& mesh,
                                 const ::PokemonInstance& unit) {
    int animIndex = unit.activeAnimIndex;
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
        animIndex = unit.currentAttackAnimIndex;
    }
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
        animIndex = unit.animMoveIndex;
    }
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
        animIndex = unit.animIdleIndex;
    }
    if (animIndex < 0 && !mesh.animations.empty()) {
        animIndex = 0;
    }
    return animIndex;
}

struct CanonicalScenePoseSample {
    float animTimeSec = 0.0f;
    std::uint32_t cacheKey = 0u;
};

std::uint32_t floatToBits(float value);

CanonicalScenePoseSample canonicalSceneAnimTimeForKey(
    const game::runtime::render_model::MeshData& mesh,
    int animIndex,
    float animTimeSec,
    float quantizeStepSec) {
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
        return {};
    }
    const float durationSec = mesh.animations[static_cast<std::size_t>(animIndex)].durationSec;
    if (!(durationSec > 0.0f)) {
        return {};
    }
    float wrapped = std::fmod(animTimeSec, durationSec);
    if (wrapped < 0.0f) {
        wrapped += durationSec;
    }
    if (wrapped == 0.0f) {
        return {};
    }
    if (!(quantizeStepSec > 0.0f)) {
        return {wrapped, floatToBits(wrapped)};
    }
    const float bucketFloat = std::round(wrapped / quantizeStepSec);
    if (!(bucketFloat > 0.0f)) {
        return {};
    }
    const float quantized = bucketFloat * quantizeStepSec;
    if (!(quantized > 0.0f) || !(quantized < durationSec)) {
        return {};
    }
    const std::uint32_t bucketIndex = static_cast<std::uint32_t>(bucketFloat);
    return {quantized, bucketIndex | 0x80000000u};
}

CanonicalScenePoseSample nextCanonicalSceneAnimTimeForKey(
    const game::runtime::render_model::MeshData& mesh,
    int animIndex,
    const CanonicalScenePoseSample& current,
    float quantizeStepSec) {
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
        return {};
    }
    const float durationSec = mesh.animations[static_cast<std::size_t>(animIndex)].durationSec;
    if (!(durationSec > 0.0f)) {
        return {};
    }
    const float prewarmStepSec =
        (quantizeStepSec > 0.0f)
            ? std::max(quantizeStepSec, kSimulationFixedStepSec)
            : kSimulationFixedStepSec;
    const float nextAnimTimeSec = current.animTimeSec + prewarmStepSec;
    if (!(nextAnimTimeSec > 0.0f) || !(nextAnimTimeSec < durationSec)) {
        return {};
    }
    return canonicalSceneAnimTimeForKey(mesh, animIndex, nextAnimTimeSec, quantizeStepSec);
}

std::uint32_t floatToBits(float value) {
    std::uint32_t bits = 0u;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct CachedScenePoseKey {
    const game::runtime::render_model::MeshData* mesh = nullptr;
    int animIndex = -1;
    std::uint32_t animSampleKey = 0u;

    bool operator==(const CachedScenePoseKey& other) const {
        return mesh == other.mesh &&
               animIndex == other.animIndex &&
               animSampleKey == other.animSampleKey;
    }
};

struct CachedScenePoseKeyHash {
    std::size_t operator()(const CachedScenePoseKey& key) const noexcept {
        const std::size_t h0 =
            std::hash<const game::runtime::render_model::MeshData*>{}(key.mesh);
        const std::size_t h1 = std::hash<int>{}(key.animIndex);
        const std::size_t h2 = std::hash<std::uint32_t>{}(key.animSampleKey);
        return (h0 * 1315423911u) ^ (h1 + 0x9e3779b9u + (h2 << 6u) + (h2 >> 2u));
    }
};

struct CachedScenePoseEntry {
    std::uint64_t lastUsedFrame = 0u;
    game::runtime::shared_backend_pose::PoseEval pose;
};

thread_local std::unordered_map<CachedScenePoseKey, CachedScenePoseEntry, CachedScenePoseKeyHash>
    g_cachedScenePoseBySignature;
thread_local std::uint64_t g_cachedScenePoseFrameCounter = 0u;

void pruneScenePoseCache(std::uint64_t frameCounter) {
    constexpr std::size_t kSoftMaxEntries = 8192u;
    constexpr std::size_t kHardMaxEntries = 16384u;
    constexpr std::uint64_t kKeepRecentFrames = 240u;
    if (g_cachedScenePoseBySignature.size() <= kSoftMaxEntries) return;

    const std::uint64_t minFrameToKeep =
        frameCounter > kKeepRecentFrames ? (frameCounter - kKeepRecentFrames) : 0u;
    for (auto it = g_cachedScenePoseBySignature.begin();
         it != g_cachedScenePoseBySignature.end();) {
        if (it->second.lastUsedFrame >= minFrameToKeep) {
            ++it;
            continue;
        }
        it = g_cachedScenePoseBySignature.erase(it);
    }

    if (g_cachedScenePoseBySignature.size() > kHardMaxEntries) {
        g_cachedScenePoseBySignature.clear();
    }
}

} // namespace

namespace game::runtime::shared_projected_units {

void drawProjectedUnits(const Args& args, const std::vector<PokemonInstance>& units) {
    if (!args.dataDb || !args.gameWorld || !args.projectedDebug || !args.sharedCaptureAttemptCache ||
        !args.sharedTailFireAnchors || !args.worldIndexedBatches || !args.backendTextureByPath ||
        !args.modelDepthTris || !args.modelDepthWorldTris || !args.remainingModelTrianglesBudget ||
        !args.worldQuads || !args.lines || !args.textLines || !args.sprites ||
        !args.worldTriangles || !args.world3DTriangles ||
        !args.sharedUnitHudCfg || !args.resolveModelMesh || !args.ensureBackendTextureLoaded ||
        !args.backendModelTriangleLimit || !args.backendModelFullMeshEnabled ||
        !args.backendModelFastTexturedPathEnabled || !args.backendModelBackfaceCullingEnabled ||
        !args.getTailFireFallbackCfg) {
        return;
    }

    const auto& dataDb = *args.dataDb;
    auto* gameWorld = args.gameWorld;
    const float worldCellSize = args.worldCellSize;
    const float minDim = args.minDim;
    const float boardSurfaceY = args.boardSurfaceY;
    const float line = args.lineThickness;
    const bool supportsWorldTriangles3D = args.supportsWorldTriangles3D;
    const bool supportsWorldIndexedMeshes = args.supportsWorldIndexedMeshes;
    const bool characterInkingEnabled = args.characterInkingEnabled;
    const bool enableGpuClipSkinning = args.enableGpuClipSkinning;
    const bool allowPortraitFallback = args.allowPortraitFallback;
    const bool forcePortraitOverlay = args.forcePortraitOverlay;
    const bool useLegacyGrowlWaveVfx = args.useLegacyGrowlWaveVfx;
    const bool useLegacyParticleVfxSnapshotBridge = args.useLegacyParticleVfxSnapshotBridge;
    const int drawableW = args.drawableW;
    const int drawableH = args.drawableH;
    const glm::vec3 cameraWorldPos = args.cameraWorldPos;

    auto& projectedDebug = *args.projectedDebug;
    auto& sharedCaptureAttemptCache = *args.sharedCaptureAttemptCache;
    auto& sharedTailFireAnchors = *args.sharedTailFireAnchors;
    auto& worldIndexedBatches = *args.worldIndexedBatches;
    auto& modelDepthTris = *args.modelDepthTris;
    auto& modelDepthWorldTris = *args.modelDepthWorldTris;
    auto& remainingModelTrianglesBudget = *args.remainingModelTrianglesBudget;
    auto& worldQuads = *args.worldQuads;
    auto& lines = *args.lines;
    auto& textLines = *args.textLines;
    auto& sprites = *args.sprites;
    auto& world3DTriangles = *args.world3DTriangles;
    std::uint32_t* visibleAnimatedUnitCount = args.visibleAnimatedUnitCount;
    const auto& sharedUnitHudCfg = *args.sharedUnitHudCfg;

    const auto& resolveModelMesh = args.resolveModelMesh;
    const auto& backendModelTriangleLimit = args.backendModelTriangleLimit;
    const auto& backendModelFullMeshEnabled = args.backendModelFullMeshEnabled;
    const auto& backendModelFastTexturedPathEnabled = args.backendModelFastTexturedPathEnabled;
    const auto& backendModelBackfaceCullingEnabled = args.backendModelBackfaceCullingEnabled;

    using BackendPoseEval = game::runtime::shared_backend_pose::PoseEval;
    using WorldIndexedBatch = game::runtime::shared_world_batches::WorldIndexedBatch;
    using SharedTailFireAnchor = game::runtime::shared_tail_fire_fallback::Anchor;
    using DepthTri = game::runtime::shared_projected_scene::DepthTri;
    using DepthWorldTri = game::runtime::shared_projected_scene::DepthWorldTri;
    using Clock = std::chrono::high_resolution_clock;
    PerfStats* perfStats = args.perfStats;
    const auto totalStart = Clock::now();
    double poseEvalMsAcc = 0.0;
    double modelRenderMsAcc = 0.0;
    double modelPrepMsAcc = 0.0;
    double modelGeometryMsAcc = 0.0;
    double overlayMsAcc = 0.0;
    std::uint32_t unitsProcessed = 0u;
    std::uint32_t modelUnits = 0u;
    std::uint32_t clipSkinnedUnits = 0u;
    std::uint32_t sharedRigidBatches = 0u;
    std::uint32_t gpuClipSkinBatches = 0u;
    std::uint32_t gpuClipPaletteBatches = 0u;
    std::uint32_t cpuRewriteBatches = 0u;
    std::uint32_t indexedBatchesQueued = 0u;

    const bool clipSkinningAdaptiveEnabled = backendClipSkinningAdaptiveEnabled();
    const float scenePoseCacheHz = backendScenePoseCacheEffectiveHz(units.size());
    const float scenePoseCacheStepSec =
        (scenePoseCacheHz > 0.0f) ? (1.0f / scenePoseCacheHz) : 0.0f;
    std::unordered_map<int, bool> clipSkinningEnabledByUnitId;
    if (clipSkinningAdaptiveEnabled) {
        std::vector<int> eligibleUnitIds;
        eligibleUnitIds.reserve(units.size());
        for (const auto& unit : units) {
            if (!unit.alive && !unit.captureInProgress && !unit.fainting) continue;
            if (!unit.alive && unit.visualScale <= 0.0001f && !unit.captureInProgress) continue;
            eligibleUnitIds.push_back(unit.id);
        }
        std::sort(eligibleUnitIds.begin(), eligibleUnitIds.end());
        const std::size_t maxClipSkinned =
            std::min<std::size_t>(backendClipSkinningMaxUnits(), eligibleUnitIds.size());
        for (std::size_t i = 0; i < maxClipSkinned; ++i) {
            clipSkinningEnabledByUnitId[eligibleUnitIds[i]] = true;
        }
    }

    const std::uint64_t poseCacheFrame = ++g_cachedScenePoseFrameCounter;
    pruneScenePoseCache(poseCacheFrame);
    std::unordered_map<std::string, const runtime::render_model::MeshData*> meshBySpecies;
    meshBySpecies.reserve(units.size());

for (const auto& unit : units) {
    if (!unit.alive && !unit.captureInProgress && !unit.fainting) continue;
    if (!unit.alive && unit.visualScale <= 0.0001f && !unit.captureInProgress) continue;
    const glm::vec3 coarseWorldPos =
        unit.position +
        glm::vec3(
            0.0f,
            unit.visualYOffset + std::max(0.2f, worldCellSize * 0.22f),
            0.0f);
    float coarseCx = 0.0f;
    float coarseCy = 0.0f;
    float coarseCz = 0.0f;
    if (!projectedDebug.projectWorld(coarseWorldPos, coarseCx, coarseCy, coarseCz)) continue;
    if (coarseCz < -0.25f || coarseCz > 1.25f) continue;
    const float coarseMarginX = std::max(96.0f, minDim * 0.18f);
    const float coarseMarginY = std::max(96.0f, minDim * 0.18f);
    if (coarseCx < -coarseMarginX ||
        coarseCx > static_cast<float>(drawableW) + coarseMarginX ||
        coarseCy < -coarseMarginY ||
        coarseCy > static_cast<float>(drawableH) + coarseMarginY) {
        continue;
    }
    ++unitsProcessed;

    const auto poseEvalStart = Clock::now();
    const runtime::render_model::MeshData* meshForUnit = nullptr;
    auto meshIt = meshBySpecies.find(unit.name);
    if (meshIt != meshBySpecies.end()) {
        meshForUnit = meshIt->second;
    } else {
        meshForUnit = resolveModelMesh(unit);
        meshBySpecies.emplace(unit.name, meshForUnit);
    }
    const BackendPoseEval* scenePose = nullptr;
    bool scenePoseReady = false;
    if (meshForUnit) {
        const int animIndex = resolveSceneAnimIndexForUnit(*meshForUnit, unit);
        const CanonicalScenePoseSample canonicalAnimSample =
            canonicalSceneAnimTimeForKey(
                *meshForUnit,
                animIndex,
                unit.animTimeSec,
                scenePoseCacheStepSec);
        const CachedScenePoseKey key{
            meshForUnit,
            animIndex,
            canonicalAnimSample.cacheKey};
        auto it = g_cachedScenePoseBySignature.find(key);
        if (it == g_cachedScenePoseBySignature.end()) {
            CachedScenePoseEntry inserted;
            game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
                *meshForUnit,
                animIndex,
                canonicalAnimSample.animTimeSec,
                true,
                inserted.pose);
            inserted.lastUsedFrame = poseCacheFrame;
            it = g_cachedScenePoseBySignature.emplace(key, std::move(inserted)).first;
        } else {
            it->second.lastUsedFrame = poseCacheFrame;
        }
        scenePose = &it->second.pose;
        scenePoseReady = true;

        if (scenePose->hasClipPose) {
            const CanonicalScenePoseSample nextAnimSample =
                nextCanonicalSceneAnimTimeForKey(
                    *meshForUnit,
                    animIndex,
                    canonicalAnimSample,
                    scenePoseCacheStepSec);
            if (nextAnimSample.cacheKey != 0u &&
                nextAnimSample.cacheKey != canonicalAnimSample.cacheKey) {
                const CachedScenePoseKey nextKey{
                    meshForUnit,
                    animIndex,
                    nextAnimSample.cacheKey};
                if (g_cachedScenePoseBySignature.find(nextKey) ==
                    g_cachedScenePoseBySignature.end()) {
                    CachedScenePoseEntry nextInserted;
                    game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
                        *meshForUnit,
                        animIndex,
                        nextAnimSample.animTimeSec,
                        true,
                        nextInserted.pose);
                    nextInserted.lastUsedFrame = poseCacheFrame;
                    g_cachedScenePoseBySignature.emplace(nextKey, std::move(nextInserted));
                }
            }
        }
    }
    const bool hasClipPoseDrivenModel = scenePoseReady && scenePose && scenePose->hasClipPose;
    runtime::render_prep_pose::ProceduralPose pose{};
    if (!hasClipPoseDrivenModel) {
        pose = runtime::render_prep_pose::computeProceduralPose(unit, worldCellSize);
    }
    poseEvalMsAcc += std::chrono::duration<double, std::milli>(Clock::now() - poseEvalStart).count();
    const bool unitClipSkinningEnabled =
        !clipSkinningAdaptiveEnabled ||
        (clipSkinningEnabledByUnitId.find(unit.id) != clipSkinningEnabledByUnitId.end());
    if (unitClipSkinningEnabled && hasClipPoseDrivenModel) {
        ++clipSkinnedUnits;
    }
    const bool applyProceduralModelMotion = !hasClipPoseDrivenModel;
    const glm::vec3 attackOffset = applyProceduralModelMotion
        ? (game::runtime::render_prep_proxy::yawForward(unit.rotation.y) * pose.attackLunge)
        : glm::vec3(0.0f);
    const float animYaw = applyProceduralModelMotion ? pose.yawDeg : unit.rotation.y;
    const float animPitch = applyProceduralModelMotion ? pose.pitchDeg : unit.rotation.x;
    const float animRoll = applyProceduralModelMotion
        ? (pose.rollDeg + (unit.side == PokemonSide::Player ? -pose.faintRoll : pose.faintRoll))
        : unit.rotation.z;
    const float attackPulse = applyProceduralModelMotion ? pose.attackPulse : 1.0f;
    const float proceduralBobY = applyProceduralModelMotion ? pose.bobY : 0.0f;
    const float proceduralFaintDrop = applyProceduralModelMotion ? pose.faintDrop : 0.0f;
    const glm::vec3 animatedCenter =
        unit.position + attackOffset +
        glm::vec3(0.0f, unit.visualYOffset + proceduralBobY - proceduralFaintDrop, 0.0f);
    const glm::vec3 worldPos =
        animatedCenter +
        glm::vec3(0.0f, std::max(0.2f, worldCellSize * 0.22f), 0.0f);
    float cx = 0.0f;
    float cy = 0.0f;
    float cz = 0.0f;
    if (!projectedDebug.projectWorld(worldPos, cx, cy, cz)) continue;
    if (cz < 0.0f || cz > 1.0f) continue;

    float sx = 0.0f;
    float sy = 0.0f;
    float sz = 0.0f;
    const bool hasCellX = projectedDebug.projectWorld(
        worldPos + glm::vec3(worldCellSize, 0.0f, 0.0f),
        sx,
        sy,
        sz);
    float cellPx = hasCellX ? glm::length(glm::vec2(sx - cx, sy - cy)) : 0.0f;
    if (!std::isfinite(cellPx) || cellPx < 8.0f) {
        cellPx = std::max(14.0f, minDim * 0.035f);
    }
    const float unitSize = std::clamp(cellPx * 0.75f, 10.0f, 84.0f);
    const game::runtime::render_prep_proxy::UnitProxyExtents extents =
        game::runtime::render_prep_proxy::computeUnitProxyExtents(unit, worldCellSize);
    const glm::vec3 proxyCenter = animatedCenter;
    const GameWorld::CaptureAttemptRenderSnapshot* captureSnapForUnit =
        unit.captureInProgress ? sharedCaptureAttemptCache.findByTarget(unit.id) : nullptr;
    float captureVisualTintStrength =
        unit.captureInProgress ? std::clamp(unit.captureTintStrength, 0.0f, 1.0f) : 0.0f;
    float captureVisualAlphaScale = 1.0f;
    const glm::vec3 captureTintColor(1.0f, 0.1f, 0.1f);

    const float renderVisualScale = (unit.fainting || !unit.alive)
        ? std::max(0.0f, unit.visualScale)
        : std::max(0.05f, unit.visualScale);
    float renderCaptureScale = (unit.fainting || !unit.alive || unit.captureInProgress)
        ? std::max(0.0f, unit.captureScale)
        : std::max(0.05f, unit.captureScale);
    if (captureSnapForUnit && captureSnapForUnit->phase == 1) {
        const float lateSuckP =
            std::clamp(captureSnapForUnit->absorbLateVisual01, 0.0f, 1.0f);
        renderCaptureScale = std::min(renderCaptureScale, std::max(0.0f, 1.0f - lateSuckP));
        captureVisualTintStrength = std::max(captureVisualTintStrength, lateSuckP);
        captureVisualAlphaScale = std::clamp(1.0f - 0.5f * lateSuckP, 0.0f, 1.0f);
    } else if (captureVisualTintStrength > 0.0f) {
        captureVisualAlphaScale =
            std::clamp(1.0f - 0.5f * captureVisualTintStrength, 0.0f, 1.0f);
    }
    const float faintFadeAlpha =
        (unit.fainting || !unit.alive)
            ? std::clamp(renderVisualScale * renderCaptureScale, 0.0f, 1.0f)
            : 1.0f;
    const float modelFadeAlpha =
        std::clamp(faintFadeAlpha * captureVisualAlphaScale, 0.0f, 1.0f);
    if ((unit.fainting || !unit.alive) &&
        (renderVisualScale <= 0.0001f || renderCaptureScale <= 0.0001f)) {
        continue;
    }

    IRenderBackend::DebugQuad tint;
    runtime::render_prep_units::applyWorldUnitTint(tint, unit);
    if (!meshForUnit) {
        const auto shadow = game::runtime::render_prep_proxy::computeShadowQuad(
            proxyCenter,
            extents.halfWidth * 1.15f,
            extents.halfDepth * 1.15f,
            animYaw,
            0.010f);
        if (supportsWorldTriangles3D) {
            projectedDebug.appendWorldQuad(
                shadow[0],
                shadow[1],
                shadow[2],
                shadow[3],
                0.02f,
                0.03f,
                0.04f,
                unit.alive ? 0.42f : 0.24f);
        } else {
            projectedDebug.appendProjectedQuad(
                shadow[0],
                shadow[1],
                shadow[2],
                shadow[3],
                0.02f,
                0.03f,
                0.04f,
                unit.alive ? 0.42f : 0.24f);
        }
    }    bool drewModelMesh = false;
    if (meshForUnit) {
        const auto modelStart = Clock::now();
        runtime::shared_projected_unit_models::PerfBreakdown modelPerf{};
        const auto modelResult = runtime::shared_projected_unit_models::renderProjectedUnitModel(
            runtime::shared_projected_unit_models::Args{
                .dataDb = &dataDb,
                .unit = &unit,
                .pose = &pose,
                .meshForUnit = meshForUnit,
                .scenePose = scenePose,
                .scenePoseReady = scenePoseReady,
                .enableClipSkinning = unitClipSkinningEnabled,
                .enableGpuClipSkinning = enableGpuClipSkinning,
                .tint = &tint,
                .worldCellSize = worldCellSize,
                .boardSurfaceY = boardSurfaceY,
                .unitSize = unitSize,
                .animPitch = animPitch,
                .animYaw = animYaw,
                .animRoll = animRoll,
                .attackPulse = attackPulse,
                .materialTimeSec = args.gameWorld ? args.gameWorld->getSharedLoopAnimTimeSec() : unit.animTimeSec,
                .renderVisualScale = renderVisualScale,
                .renderCaptureScale = renderCaptureScale,
                .captureVisualTintStrength = captureVisualTintStrength,
                .modelFadeAlpha = modelFadeAlpha,
                .captureTintColor = captureTintColor,
                .proxyCenter = proxyCenter,
                .cameraWorldPos = cameraWorldPos,
                .supportsWorldTriangles3D = supportsWorldTriangles3D,
                .supportsWorldIndexedMeshes = supportsWorldIndexedMeshes,
                .characterInkingEnabled = characterInkingEnabled,
                .projectedDebug = &projectedDebug,
                .sharedTailFireAnchors = &sharedTailFireAnchors,
                .worldIndexedBatches = &worldIndexedBatches,
                .backendTextureByPath = args.backendTextureByPath,
                .modelDepthTris = &modelDepthTris,
                .modelDepthWorldTris = &modelDepthWorldTris,
                .remainingModelTrianglesBudget = &remainingModelTrianglesBudget,
                .world3DTriangles = &world3DTriangles,
                .ensureBackendTextureLoaded = args.ensureBackendTextureLoaded,
                .backendModelTriangleLimit = backendModelTriangleLimit,
                .backendModelFullMeshEnabled = backendModelFullMeshEnabled,
                .backendModelFastTexturedPathEnabled = backendModelFastTexturedPathEnabled,
                .backendModelBackfaceCullingEnabled = backendModelBackfaceCullingEnabled,
                .perfBreakdown = &modelPerf});
        modelRenderMsAcc += std::chrono::duration<double, std::milli>(Clock::now() - modelStart).count();
        modelPrepMsAcc += modelPerf.prepMs;
        modelGeometryMsAcc += modelPerf.geometryMs;
        sharedRigidBatches += modelPerf.sharedRigidBatches;
        gpuClipSkinBatches += modelPerf.gpuClipSkinBatches;
        gpuClipPaletteBatches += modelPerf.gpuClipPaletteBatches;
        cpuRewriteBatches += modelPerf.cpuRewriteBatches;
        indexedBatchesQueued += modelPerf.indexedBatchesQueued;
        if (modelResult.skipUnit) continue;
        drewModelMesh = modelResult.drewModelMesh;
        if (drewModelMesh) ++modelUnits;
    }

    if (!drewModelMesh) {
        float topR = std::clamp(tint.r * 0.86f + 0.12f, 0.0f, 1.0f);
        float topG = std::clamp(tint.g * 0.86f + 0.12f, 0.0f, 1.0f);
        float topB = std::clamp(tint.b * 0.86f + 0.12f, 0.0f, 1.0f);
        float sideR = std::clamp(tint.r * 0.72f, 0.0f, 1.0f);
        float sideG = std::clamp(tint.g * 0.72f, 0.0f, 1.0f);
        float sideB = std::clamp(tint.b * 0.72f, 0.0f, 1.0f);
        float topAlpha = unit.alive ? 0.96f : 0.78f;
        float sideAlpha = unit.alive ? 0.88f : 0.70f;
        if (captureVisualTintStrength > 0.001f) {
            const glm::vec3 topTinted = glm::mix(
                glm::vec3(topR, topG, topB),
                captureTintColor,
                captureVisualTintStrength);
            const glm::vec3 sideTinted = glm::mix(
                glm::vec3(sideR, sideG, sideB),
                captureTintColor,
                captureVisualTintStrength);
            topR = topTinted.r;
            topG = topTinted.g;
            topB = topTinted.b;
            sideR = sideTinted.r;
            sideG = sideTinted.g;
            sideB = sideTinted.b;
            topAlpha *= captureVisualAlphaScale;
            sideAlpha *= captureVisualAlphaScale;
        }
        const game::runtime::render_prep_proxy::UnitProxyCorners corners =
            game::runtime::render_prep_proxy::computeUnitProxyCorners(
                proxyCenter,
                extents,
                animYaw);
        if (supportsWorldTriangles3D) {
            projectedDebug.appendWorldQuad(
                corners.top[0],
                corners.top[1],
                corners.top[2],
                corners.top[3],
                topR, topG, topB, topAlpha);
            projectedDebug.appendWorldQuad(
                corners.bottom[0], corners.bottom[1], corners.top[1], corners.top[0],
                sideR, sideG, sideB, sideAlpha);
            projectedDebug.appendWorldQuad(
                corners.bottom[1], corners.bottom[2], corners.top[2], corners.top[1],
                sideR, sideG, sideB, sideAlpha);
            projectedDebug.appendWorldQuad(
                corners.bottom[2], corners.bottom[3], corners.top[3], corners.top[2],
                sideR, sideG, sideB, sideAlpha);
            projectedDebug.appendWorldQuad(
                corners.bottom[3], corners.bottom[0], corners.top[0], corners.top[3],
                sideR, sideG, sideB, sideAlpha);
        } else {
            projectedDebug.appendProjectedQuad(
                corners.top[0],
                corners.top[1],
                corners.top[2],
                corners.top[3],
                topR, topG, topB, topAlpha);
            projectedDebug.appendProjectedQuad(
                corners.bottom[0], corners.bottom[1], corners.top[1], corners.top[0],
                sideR, sideG, sideB, sideAlpha);
            projectedDebug.appendProjectedQuad(
                corners.bottom[1], corners.bottom[2], corners.top[2], corners.top[1],
                sideR, sideG, sideB, sideAlpha);
            projectedDebug.appendProjectedQuad(
                corners.bottom[2], corners.bottom[3], corners.top[3], corners.top[2],
                sideR, sideG, sideB, sideAlpha);
            projectedDebug.appendProjectedQuad(
                corners.bottom[3], corners.bottom[0], corners.top[0], corners.top[3],
                sideR, sideG, sideB, sideAlpha);
        }
    }
    if (visibleAnimatedUnitCount) {
        ++(*visibleAnimatedUnitCount);
    }

    const auto overlayStart = Clock::now();
    runtime::shared_projected_unit_overlays::appendProjectedUnitOverlays(
        runtime::shared_projected_unit_overlays::Args{
            .unit = &unit,
            .drewModelMesh = drewModelMesh,
            .allowPortraitFallback = allowPortraitFallback,
            .forcePortraitOverlay = forcePortraitOverlay,
            .useLegacyGrowlWaveVfx = useLegacyGrowlWaveVfx,
            .useLegacyParticleVfxSnapshotBridge = useLegacyParticleVfxSnapshotBridge,
            .gameWorld = gameWorld,
            .projectedDebug = &projectedDebug,
            .worldQuads = &worldQuads,
            .lines = &lines,
            .textLines = &textLines,
            .sprites = &sprites,
            .sharedUnitHudCfg = &sharedUnitHudCfg,
            .cx = cx,
            .cy = cy,
            .unitSize = unitSize,
            .minDim = minDim,
            .cellPx = cellPx,
            .lineThickness = line,
            .worldCellSize = worldCellSize,
            .animYaw = animYaw,
            .proxyCenter = proxyCenter,
            .extents = extents});
    overlayMsAcc += std::chrono::duration<double, std::milli>(Clock::now() - overlayStart).count();
}

if (perfStats) {
    perfStats->totalMs += std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
    perfStats->poseEvalMs += poseEvalMsAcc;
    perfStats->modelRenderMs += modelRenderMsAcc;
    perfStats->modelPrepMs += modelPrepMsAcc;
    perfStats->modelGeometryMs += modelGeometryMsAcc;
    perfStats->overlayMs += overlayMsAcc;
    perfStats->unitsProcessed += unitsProcessed;
    perfStats->modelUnits += modelUnits;
    perfStats->clipSkinnedUnits += clipSkinnedUnits;
    perfStats->sharedRigidBatches += sharedRigidBatches;
    perfStats->gpuClipSkinBatches += gpuClipSkinBatches;
    perfStats->gpuClipPaletteBatches += gpuClipPaletteBatches;
    perfStats->cpuRewriteBatches += cpuRewriteBatches;
    perfStats->indexedBatchesQueued += indexedBatchesQueued;
}

}

} // namespace game::runtime::shared_projected_units





