#include "game/runtime/session/SessionWorldRenderRuntime.h"

#include "engine/core/EngineServices.h"
#include "engine/core/ecs/World.h"
#include "engine/render/Camera3D.h"
#include "engine/render/IRenderBackend.h"
#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/runtime/routes/BackendRenderPolicy.h"
#include "game/runtime/session/SessionBackendInventoryUi.h"
#include "game/runtime/session/SessionFrameMetrics.h"
#include "game/runtime/session/SessionLegacyWorldView.h"
#include "game/runtime/session/SessionProjectedWorldView.h"
#include "game/runtime/session/SessionRenderConfig.h"
#include "game/runtime/session/SessionRenderLayout.h"
#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/session/SessionWorldBackdrop.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"
#include "game/runtime/shared/ui/SharedBackendDebugViewOverlay.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "game/runtime/ui/InventoryPanel.h"
#include "game/world/GameWorld.h"

#include <algorithm>
#include <chrono>

namespace game::runtime::session_world_render_runtime {

std::size_t render(const Args& args) {
    if (!args.renderer || !args.config || !args.drawableW || !args.drawableH) return 0u;

    using RenderBuildClock = std::chrono::steady_clock;
    const auto worldComposeStart = RenderBuildClock::now();
    const bool useLegacyGrowlWaveVfx =
        game::runtime::session_render_config::backendUseLegacyGrowlWaveVfxEnabled();
    const bool useLegacyParticleVfxSnapshotBridge =
        game::runtime::session_render_config::backendUseLegacyParticleVfxSnapshotBridgeEnabled();

    if (args.backendInventoryPanel) {
        ui_inventory_panel::clearHitRegions(*args.backendInventoryPanel);
    }

    auto& scratch = game::runtime::session_render_scratch::threadScratch();
    game::runtime::session_render_scratch::ensureCapacity(scratch);
    auto& worldBackgroundQuads = scratch.worldBackgroundQuads;
    auto& worldQuads = scratch.worldQuads;
    auto& worldTriangles = scratch.worldTriangles;
    auto& world3DTriangles = scratch.world3DTriangles;
    auto& worldIndexedBatches = scratch.worldIndexedBatches;
    auto& overlayQuads = scratch.overlayQuads;
    auto& lines = scratch.lines;
    auto& textLines = scratch.textLines;
    auto& sprites = scratch.sprites;
    std::uint32_t visibleAnimatedUnitsThisFrame = 0u;
    std::uint32_t particleCountThisFrame = 0u;
    float projectedUnitsMsThisFrame = 0.0f;
    float projectedPoseEvalMsThisFrame = 0.0f;
    float projectedModelMsThisFrame = 0.0f;
    float projectedModelPrepMsThisFrame = 0.0f;
    float projectedModelGeometryMsThisFrame = 0.0f;
    float projectedOverlayMsThisFrame = 0.0f;
    std::uint32_t projectedUnitsProcessedThisFrame = 0u;
    std::uint32_t projectedModelUnitsThisFrame = 0u;
    std::uint32_t projectedClipSkinnedUnitsThisFrame = 0u;
    std::uint32_t projectedSharedRigidBatchesThisFrame = 0u;
    std::uint32_t projectedGpuClipSkinBatchesThisFrame = 0u;
    std::uint32_t projectedGpuClipPaletteBatchesThisFrame = 0u;
    std::uint32_t projectedCpuRewriteBatchesThisFrame = 0u;
    std::uint32_t projectedIndexedBatchesQueuedThisFrame = 0u;
    float worldBackdropComposeMsThisFrame = 0.0f;
    float worldVfxBridgeMsThisFrame = 0.0f;
    float worldDepthFlushMsThisFrame = 0.0f;

    const bool supportsWorldTriangles3D = args.renderer->supportsWorldTriangles3D();
    const bool supportsWorldIndexedMeshes = args.renderer->supportsWorldIndexedMeshes();
    const bool allowPortraitFallback =
        game::runtime::session_render_config::backendWorldPortraitFallbackEnabled();
    const bool forcePortraitOverlay =
        game::runtime::session_render_config::backendWorldPortraitOverlayForced();
    float worldViewProj[16] = {};
    bool hasWorldViewProj = false;
    float cameraWorldPos3[3] = {0.0f, 7.0f, 9.0f};
    float cameraForward3[3] = {0.0f, -0.6139406f, -0.7893522f};
    float cameraTarget3[3] = {0.0f, -1.0f, 0.0f};
    const auto layout =
        game::runtime::session_render_layout::build(*args.config, args.drawableW, args.drawableH);

    const bool showWorldBackdrop = runtime::render::shouldRenderBackendWorldBackdrop(
        args.routes,
        args.renderWorld,
        args.allowBackendMenuBackdrop);
    const bool useProjectedWorldLayout =
        showWorldBackdrop && args.renderWorld && args.gameWorld && (args.camera != nullptr);
    const auto backdropTheme =
        game::runtime::session_world_backdrop::routeThemeFromScriptPath(args.stateScriptPath);

    if (args.services &&
        scratch.lastGraphicsQualityGeneration != args.services->graphicsQualityGeneration) {
        game::runtime::session_render_scratch::resetSceneCaches(scratch);
        scratch.lastGraphicsQualityGeneration = args.services->graphicsQualityGeneration;
    }

    game::runtime::session_render_scratch::beginFrame(
        scratch,
        useProjectedWorldLayout,
        args.renderer);
    if (showWorldBackdrop) {
        if (useProjectedWorldLayout) {
            const auto projectedWorld =
                game::runtime::session_projected_world_view::appendProjectedWorldView(
                    {
                        .renderer = args.renderer,
                        .gameWorld = args.gameWorld,
                        .camera = args.camera,
                        .dataDb = args.dataDb,
                        .scratch = &scratch,
                        .backendTextureByPath = args.backendTextureByPath,
                        .sharedUnitHudCfg = layout.sharedUnitHudCfg,
                        .supportsWorldTriangles3D = supportsWorldTriangles3D,
                        .supportsWorldIndexedMeshes = supportsWorldIndexedMeshes,
                        .characterInkingEnabled =
                            (args.services ? args.services->characterInkingEnabled : false),
                        .graphicsQuality =
                            (args.services ? args.services->graphicsQuality : 3),
                        .enableGpuClipSkinning =
                            game::runtime::session_render_config::backendGpuClipSkinningEnabled(args.renderer),
                        .allowPortraitFallback = allowPortraitFallback,
                        .forcePortraitOverlay = forcePortraitOverlay,
                        .useLegacyGrowlWaveVfx = useLegacyGrowlWaveVfx,
                        .useLegacyParticleVfxSnapshotBridge =
                            useLegacyParticleVfxSnapshotBridge,
                        .useExactTailFireCpuPath =
                            game::runtime::session_render_config::backendUseExactTailFireCpuPathEnabled(),
                        .backdropTheme = backdropTheme,
                        .drawableW = args.drawableW,
                        .drawableH = args.drawableH,
                        .rows = layout.rows,
                        .cols = layout.cols,
                        .benchSlots = args.config->benchSlots,
                        .minDim = layout.minDim,
                        .boardX = layout.boardX,
                        .boardY = layout.boardY,
                        .boardW = layout.boardW,
                        .boardH = layout.boardH,
                        .cellW = layout.cellW,
                        .cellH = layout.cellH,
                        .simNowSec = args.simNowSec,
                        .ensureBackendMeshLoaded = args.ensureBackendMeshLoaded,
                        .ensureBackendTextureLoaded = args.ensureBackendTextureLoaded,
                    });
            hasWorldViewProj = projectedWorld.hasWorldViewProj;
            std::copy(
                projectedWorld.worldViewProj.begin(),
                projectedWorld.worldViewProj.end(),
                worldViewProj);
            std::copy(
                projectedWorld.cameraWorldPos.begin(),
                projectedWorld.cameraWorldPos.end(),
                cameraWorldPos3);
            std::copy(
                projectedWorld.cameraForward.begin(),
                projectedWorld.cameraForward.end(),
                cameraForward3);
            std::copy(
                projectedWorld.cameraTarget.begin(),
                projectedWorld.cameraTarget.end(),
                cameraTarget3);
            visibleAnimatedUnitsThisFrame += projectedWorld.visibleAnimatedUnits;
            projectedUnitsMsThisFrame = projectedWorld.projectedUnitsMs;
            projectedPoseEvalMsThisFrame = projectedWorld.projectedPoseEvalMs;
            projectedModelMsThisFrame = projectedWorld.projectedModelMs;
            projectedModelPrepMsThisFrame = projectedWorld.projectedModelPrepMs;
            projectedModelGeometryMsThisFrame = projectedWorld.projectedModelGeometryMs;
            projectedOverlayMsThisFrame = projectedWorld.projectedOverlayMs;
            projectedUnitsProcessedThisFrame = projectedWorld.projectedUnitsProcessed;
            projectedModelUnitsThisFrame = projectedWorld.projectedModelUnits;
            projectedClipSkinnedUnitsThisFrame = projectedWorld.projectedClipSkinnedUnits;
            projectedSharedRigidBatchesThisFrame = projectedWorld.projectedSharedRigidBatches;
            projectedGpuClipSkinBatchesThisFrame = projectedWorld.projectedGpuClipSkinBatches;
            projectedGpuClipPaletteBatchesThisFrame =
                projectedWorld.projectedGpuClipPaletteBatches;
            projectedCpuRewriteBatchesThisFrame = projectedWorld.projectedCpuRewriteBatches;
            projectedIndexedBatchesQueuedThisFrame =
                projectedWorld.projectedIndexedBatchesQueued;
            worldBackdropComposeMsThisFrame = projectedWorld.worldBackdropComposeMs;
            worldVfxBridgeMsThisFrame = projectedWorld.worldVfxBridgeMs;
            worldDepthFlushMsThisFrame = projectedWorld.worldDepthFlushMs;
        } else {
            visibleAnimatedUnitsThisFrame +=
                game::runtime::session_legacy_world_view::appendLegacyWorldView(
                    {
                        .renderWorld = args.renderWorld,
                        .gameWorld = args.gameWorld,
                        .drawableW = args.drawableW,
                        .drawableH = args.drawableH,
                        .rows = layout.rows,
                        .cols = layout.cols,
                        .benchSlots = args.config->benchSlots,
                        .minDim = layout.minDim,
                        .boardX = layout.boardX,
                        .boardY = layout.boardY,
                        .boardW = layout.boardW,
                        .boardH = layout.boardH,
                        .cellW = layout.cellW,
                        .cellH = layout.cellH,
                        .sharedUnitHudCfg = layout.sharedUnitHudCfg,
                    },
                    scratch).visibleAnimatedUnits;
        }
    }
    if (args.renderWorld && args.gameWorld) {
        particleCountThisFrame = args.gameWorld->countActiveParticleVfx();
    }
    if (args.prewarmWorldIndexedOnly) {
        if (!worldIndexedBatches.empty() && hasWorldViewProj && supportsWorldIndexedMeshes) {
            return runtime::shared_world_batches::prewarmWorldIndexedBatches(
                *args.renderer,
                worldIndexedBatches,
                cameraWorldPos3,
                cameraForward3,
                cameraTarget3);
        }
        return 0u;
    }

    const auto worldComposeEnd = RenderBuildClock::now();
    game::runtime::session_frame_metrics::publish(
        args.engineServices,
        {
            .visibleAnimatedUnits = visibleAnimatedUnitsThisFrame,
            .particleCount = particleCountThisFrame,
            .projectedUnitsMs = projectedUnitsMsThisFrame,
            .projectedPoseEvalMs = projectedPoseEvalMsThisFrame,
            .projectedModelMs = projectedModelMsThisFrame,
            .projectedModelPrepMs = projectedModelPrepMsThisFrame,
            .projectedModelGeometryMs = projectedModelGeometryMsThisFrame,
            .projectedOverlayMs = projectedOverlayMsThisFrame,
            .projectedUnitsProcessed = projectedUnitsProcessedThisFrame,
            .projectedModelUnits = projectedModelUnitsThisFrame,
            .projectedClipSkinnedUnits = projectedClipSkinnedUnitsThisFrame,
            .projectedSharedRigidBatches = projectedSharedRigidBatchesThisFrame,
            .projectedGpuClipSkinBatches = projectedGpuClipSkinBatchesThisFrame,
            .projectedGpuClipPaletteBatches = projectedGpuClipPaletteBatchesThisFrame,
            .projectedCpuRewriteBatches = projectedCpuRewriteBatchesThisFrame,
            .projectedIndexedBatchesQueued = projectedIndexedBatchesQueuedThisFrame,
            .worldComposeMs = static_cast<float>(
                std::chrono::duration<double, std::milli>(worldComposeEnd - worldComposeStart).count()),
            .worldBackdropMs = worldBackdropComposeMsThisFrame,
            .worldVfxMs = worldVfxBridgeMsThisFrame,
            .worldDepthFlushMs = worldDepthFlushMsThisFrame,
        });

    runtime::shared_backend_debug_view::ComposeAndSubmitArgs overlayArgs;
    overlayArgs.renderer = args.renderer;
    overlayArgs.engineServices = args.engineServices;
    overlayArgs.services = args.services;
    overlayArgs.gameWorld = args.gameWorld;
    overlayArgs.camera = args.camera;
    overlayArgs.ecsWorld = args.ecsWorld;
    overlayArgs.roundPhaseEntity = args.roundPhaseEntity;
    overlayArgs.log = args.log;
    overlayArgs.backendInventoryPanel = args.backendInventoryPanel;
    overlayArgs.refreshBackendInventoryFromWorld = args.refreshBackendInventoryFromWorld;
    overlayArgs.showPerfOverlay = args.showPerfOverlay;
    overlayArgs.renderWorld = args.renderWorld;
    overlayArgs.hasWorldViewProj = hasWorldViewProj;
    overlayArgs.supportsWorldTriangles3D = supportsWorldTriangles3D;
    overlayArgs.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
    overlayArgs.drawableW = args.drawableW;
    overlayArgs.drawableH = args.drawableH;
    overlayArgs.edgePad = layout.edgePad;
    overlayArgs.lineStep = layout.lineStep;
    overlayArgs.uiScale = layout.uiScale;
    overlayArgs.worldViewProj = hasWorldViewProj ? worldViewProj : nullptr;
    overlayArgs.renderBuildBreakdown =
        args.engineServices ? &args.engineServices->frameRenderBuildBreakdown : nullptr;
    overlayArgs.worldBackgroundQuads = &worldBackgroundQuads;
    overlayArgs.worldQuads = &worldQuads;
    overlayArgs.worldTriangles = &worldTriangles;
    overlayArgs.world3DTriangles = &world3DTriangles;
    const auto worldSceneView = game::runtime::shared_world_scene::buildWorldSceneView(
        scratch.worldSceneRegistry,
        hasWorldViewProj ? worldViewProj : nullptr,
        args.drawableW,
        args.drawableH,
        cameraWorldPos3,
        cameraForward3,
        cameraTarget3);
    overlayArgs.worldSceneView = &worldSceneView;
    overlayArgs.worldSceneFrame = &scratch.worldSceneFrame;
    overlayArgs.worldIndexedBatches = &worldIndexedBatches;
    overlayArgs.overlayQuads = &overlayQuads;
    overlayArgs.lines = &lines;
    overlayArgs.textLines = &textLines;
    overlayArgs.sprites = &sprites;
    runtime::shared_backend_debug_view::composeAndSubmit(overlayArgs);
    return 0u;
}

} // namespace game::runtime::session_world_render_runtime
