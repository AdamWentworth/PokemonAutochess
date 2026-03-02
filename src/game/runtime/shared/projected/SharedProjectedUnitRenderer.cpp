#include "game/runtime/shared/projected/SharedProjectedUnitRenderer.h"

#include "game/runtime/BackendProceduralPose.h"
#include "game/runtime/BackendMaterialShading.h"
#include "game/runtime/BackendUnitVisuals.h"
#include "game/runtime/BackendWorldProxyGeometry.h"
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
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {
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
    const bool hasWorldViewProj = args.hasWorldViewProj;
    const bool allowPortraitFallback = args.allowPortraitFallback;
    const bool forcePortraitOverlay = args.forcePortraitOverlay;
    const bool useLegacyGrowlWaveVfx = args.useLegacyGrowlWaveVfx;
    const bool useLegacyParticleVfxSnapshotBridge = args.useLegacyParticleVfxSnapshotBridge;
    const float* worldViewProj = args.worldViewProj;
    const int drawableW = args.drawableW;
    const int drawableH = args.drawableH;
    const glm::vec3 cameraWorldPos = args.cameraWorldPos;

    auto& projectedDebug = *args.projectedDebug;
    auto& sharedCaptureAttemptCache = *args.sharedCaptureAttemptCache;
    auto& sharedTailFireAnchors = *args.sharedTailFireAnchors;
    auto& worldIndexedBatches = *args.worldIndexedBatches;
    auto& backendTextureByPath = *args.backendTextureByPath;
    auto& modelDepthTris = *args.modelDepthTris;
    auto& modelDepthWorldTris = *args.modelDepthWorldTris;
    auto& remainingModelTrianglesBudget = *args.remainingModelTrianglesBudget;
    auto& worldQuads = *args.worldQuads;
    auto& lines = *args.lines;
    auto& textLines = *args.textLines;
    auto& sprites = *args.sprites;
    auto& worldTriangles = *args.worldTriangles;
    auto& world3DTriangles = *args.world3DTriangles;
    std::uint32_t* visibleAnimatedUnitCount = args.visibleAnimatedUnitCount;
    const auto& sharedUnitHudCfg = *args.sharedUnitHudCfg;

    const auto& resolveModelMesh = args.resolveModelMesh;
    const auto& ensureBackendTextureLoaded = args.ensureBackendTextureLoaded;
    const auto& backendModelTriangleLimit = args.backendModelTriangleLimit;
    const auto& backendModelFullMeshEnabled = args.backendModelFullMeshEnabled;
    const auto& backendModelFastTexturedPathEnabled = args.backendModelFastTexturedPathEnabled;
    const auto& backendModelBackfaceCullingEnabled = args.backendModelBackfaceCullingEnabled;
    const auto& getTailFireFallbackCfg = args.getTailFireFallbackCfg;

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
    double overlayMsAcc = 0.0;
    std::uint32_t unitsProcessed = 0u;
    std::uint32_t modelUnits = 0u;
    std::uint32_t clipSkinnedUnits = 0u;

    const bool clipSkinningAdaptiveEnabled = backendClipSkinningAdaptiveEnabled();
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


for (const auto& unit : units) {
    if (!unit.alive && !unit.captureInProgress && !unit.fainting) continue;
    if (!unit.alive && unit.visualScale <= 0.0001f && !unit.captureInProgress) continue;
    ++unitsProcessed;

    const auto poseEvalStart = Clock::now();
    const runtime::backend_anim::ProceduralPose pose =
        runtime::backend_anim::computeProceduralPose(unit, worldCellSize);
    const runtime::backend_model::MeshData* meshForUnit = resolveModelMesh(unit);
    BackendPoseEval scenePose;
    bool scenePoseReady = false;
    if (meshForUnit) {
        scenePose = game::runtime::shared_backend_pose::evaluateScenePose(*meshForUnit, unit);
        scenePoseReady = true;
    }
    poseEvalMsAcc += std::chrono::duration<double, std::milli>(Clock::now() - poseEvalStart).count();
    const bool unitClipSkinningEnabled =
        !clipSkinningAdaptiveEnabled ||
        (clipSkinningEnabledByUnitId.find(unit.id) != clipSkinningEnabledByUnitId.end());
    if (unitClipSkinningEnabled && scenePoseReady && scenePose.hasClipPose) {
        ++clipSkinnedUnits;
    }
    const bool hasClipPoseDrivenModel = scenePoseReady && scenePose.hasClipPose;
    const bool applyProceduralModelMotion = !hasClipPoseDrivenModel;
    const glm::vec3 attackOffset = applyProceduralModelMotion
        ? (game::runtime::backend_proxy::yawForward(unit.rotation.y) * pose.attackLunge)
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
    const game::runtime::backend_proxy::UnitProxyExtents extents =
        game::runtime::backend_proxy::computeUnitProxyExtents(unit, worldCellSize);
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
    runtime::backend_units::applyWorldUnitTint(tint, unit);
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
    if (!meshForUnit) {
        const auto shadow = game::runtime::backend_proxy::computeShadowQuad(
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
        const auto modelResult = runtime::shared_projected_unit_models::renderProjectedUnitModel(
            runtime::shared_projected_unit_models::Args{
                .dataDb = &dataDb,
                .unit = &unit,
                .pose = &pose,
                .meshForUnit = meshForUnit,
                .scenePose = &scenePose,
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
                .modelDepthTris = &modelDepthTris,
                .modelDepthWorldTris = &modelDepthWorldTris,
                .remainingModelTrianglesBudget = &remainingModelTrianglesBudget,
                .world3DTriangles = &world3DTriangles,
                .backendModelTriangleLimit = backendModelTriangleLimit,
                .backendModelFullMeshEnabled = backendModelFullMeshEnabled,
                .backendModelFastTexturedPathEnabled = backendModelFastTexturedPathEnabled,
                .backendModelBackfaceCullingEnabled = backendModelBackfaceCullingEnabled});
        modelRenderMsAcc += std::chrono::duration<double, std::milli>(Clock::now() - modelStart).count();
        if (modelResult.skipUnit) continue;
        drewModelMesh = modelResult.drewModelMesh;
        if (drewModelMesh) ++modelUnits;
    }

    const game::runtime::backend_proxy::UnitProxyCorners corners =
        game::runtime::backend_proxy::computeUnitProxyCorners(
            proxyCenter,
            extents,
            animYaw);
    if (!drewModelMesh) {
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
    perfStats->overlayMs += overlayMsAcc;
    perfStats->unitsProcessed += unitsProcessed;
    perfStats->modelUnits += modelUnits;
    perfStats->clipSkinnedUnits += clipSkinnedUnits;
}

}

} // namespace game::runtime::shared_projected_units


