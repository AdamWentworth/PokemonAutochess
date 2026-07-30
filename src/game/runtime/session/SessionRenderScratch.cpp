#include "game/runtime/session/SessionRenderScratch.h"

namespace game::runtime::session_render_scratch {

bool ProjectedBackdropCacheKey::operator==(const ProjectedBackdropCacheKey& other) const {
    return supportsWorldTriangles3D == other.supportsWorldTriangles3D &&
           supportsWorldIndexedMeshes == other.supportsWorldIndexedMeshes &&
           enableBackdropTiles == other.enableBackdropTiles &&
           rows == other.rows &&
           cols == other.cols &&
           benchSlots == other.benchSlots &&
           graphicsQuality == other.graphicsQuality &&
           worldCellSize == other.worldCellSize &&
           boardMinX == other.boardMinX &&
           boardMinZ == other.boardMinZ &&
           boardMaxX == other.boardMaxX &&
           boardMaxZ == other.boardMaxZ &&
           drawableW == other.drawableW &&
           drawableH == other.drawableH &&
           boardX == other.boardX &&
           boardY == other.boardY &&
           boardW == other.boardW &&
           boardH == other.boardH &&
           cellW == other.cellW &&
           cellH == other.cellH &&
           line == other.line &&
           arenaBackdropTheme == other.arenaBackdropTheme &&
           route1BackdropScaleMul == other.route1BackdropScaleMul &&
           route1BackdropOffsetXCells == other.route1BackdropOffsetXCells &&
           route1BackdropOffsetY == other.route1BackdropOffsetY &&
           route1BackdropOffsetZCells == other.route1BackdropOffsetZCells &&
           route1BackdropYawDeg == other.route1BackdropYawDeg &&
           canonicalRoute1Environment ==
               other.canonicalRoute1Environment;
}

RenderScratch& threadScratch() {
    static thread_local RenderScratch scratch;
    return scratch;
}

void ensureCapacity(RenderScratch& scratch) {
    if (scratch.worldBackgroundQuads.capacity() < 1024u) scratch.worldBackgroundQuads.reserve(1024u);
    if (scratch.worldQuads.capacity() < 1024u) scratch.worldQuads.reserve(1024u);
    if (scratch.worldTriangles.capacity() < 4096u) scratch.worldTriangles.reserve(4096u);
    if (scratch.world3DTriangles.capacity() < 120000u) scratch.world3DTriangles.reserve(120000u);
    if (scratch.worldIndexedBatches.capacity() < 64u) scratch.worldIndexedBatches.reserve(64u);
    if (scratch.overlayQuads.capacity() < 1024u) scratch.overlayQuads.reserve(1024u);
    if (scratch.lines.capacity() < 512u) scratch.lines.reserve(512u);
    if (scratch.textLines.capacity() < 8192u) scratch.textLines.reserve(8192u);
    if (scratch.sprites.capacity() < 256u) scratch.sprites.reserve(256u);
    if (scratch.unitLabels.capacity() < 64u) scratch.unitLabels.reserve(64u);
    if (scratch.projectedRenderItems.entries.bucket_count() < 256u) {
        scratch.projectedRenderItems.entries.reserve(256u);
    }
    if (scratch.worldSceneRegistry.geometries.capacity() < 64u) {
        scratch.worldSceneRegistry.geometries.reserve(64u);
    }
    if (scratch.worldSceneRegistry.materials.capacity() < 64u) {
        scratch.worldSceneRegistry.materials.reserve(64u);
    }
    if (scratch.worldSceneRegistry.renderObjects.capacity() < 64u) {
        scratch.worldSceneRegistry.renderObjects.reserve(64u);
    }
    if (scratch.worldSceneRegistry.geometryByIdentity.bucket_count() < 128u) {
        scratch.worldSceneRegistry.geometryByIdentity.reserve(128u);
    }
    if (scratch.worldSceneRegistry.materialByIdentity.bucket_count() < 128u) {
        scratch.worldSceneRegistry.materialByIdentity.reserve(128u);
    }
    if (scratch.worldSceneFrame.drawClasses.capacity() < 32u) {
        scratch.worldSceneFrame.drawClasses.reserve(32u);
    }
    if (scratch.sharedTailFireAnchors.bucket_count() < 16u) scratch.sharedTailFireAnchors.reserve(16u);
    if (scratch.sharedCaptureAttemptCache.snaps.capacity() < 8u) scratch.sharedCaptureAttemptCache.snaps.reserve(8u);
    if (scratch.sharedCaptureAttemptCache.byTargetId.bucket_count() < 8u) {
        scratch.sharedCaptureAttemptCache.byTargetId.reserve(8u);
    }
}

void invalidateProjectedBackdrop(RenderScratch& scratch) {
    scratch.projectedBackdropValid = false;
    scratch.projectedBackdropWorldBackgroundQuadsCount = 0u;
    scratch.projectedBackdropWorldTrianglesCount = 0u;
    scratch.projectedBackdropWorld3DTrianglesCount = 0u;
    scratch.projectedBackdropWorldIndexedBatchesCount = 0u;
    scratch.projectedBackdropLinesCount = 0u;
}

void resetSceneCaches(RenderScratch& scratch) {
    shared_projected_render_items::resetProjectedRenderItems(
        scratch.projectedRenderItems);
    shared_world_scene::resetWorldSceneRegistry(scratch.worldSceneRegistry);
    shared_world_scene::beginWorldSceneFrame(scratch.worldSceneFrame);
}

void beginFrame(RenderScratch& scratch,
                bool useProjectedWorldLayout,
                IRenderBackend* renderer) {
    const bool reuseProjectedBackdrop =
        useProjectedWorldLayout && scratch.projectedBackdropValid;

    shared_projected_render_items::beginProjectedRenderItemsFrame(
        scratch.projectedRenderItems);
    shared_world_scene::beginWorldSceneFrame(scratch.worldSceneFrame);
    scratch.worldQuads.clear();
    if (!reuseProjectedBackdrop) {
        scratch.worldIndexedBatches.clear();
    }
    scratch.overlayQuads.clear();
    scratch.textLines.clear();
    scratch.sprites.clear();
    scratch.unitLabels.clear();
    scratch.sharedTailFireAnchors.clear();
    scratch.sharedCaptureAttemptCache.snaps.clear();
    scratch.sharedCaptureAttemptCache.byTargetId.clear();

    if (reuseProjectedBackdrop) {
        scratch.worldBackgroundQuads.resize(scratch.projectedBackdropWorldBackgroundQuadsCount);
        scratch.worldTriangles.resize(scratch.projectedBackdropWorldTrianglesCount);
        scratch.world3DTriangles.resize(scratch.projectedBackdropWorld3DTrianglesCount);
        scratch.worldIndexedBatches.resize(scratch.projectedBackdropWorldIndexedBatchesCount);
        scratch.lines.resize(scratch.projectedBackdropLinesCount);
        if (!renderer || !renderer->supportsWorldSceneFastPath()) {
            shared_world_scene::resetWorldSceneRegistry(scratch.worldSceneRegistry);
        }
        return;
    }

    scratch.worldBackgroundQuads.clear();
    scratch.worldTriangles.clear();
    scratch.world3DTriangles.clear();
    scratch.lines.clear();
    if (!useProjectedWorldLayout) {
        invalidateProjectedBackdrop(scratch);
        resetSceneCaches(scratch);
    } else if (!renderer || !renderer->supportsWorldSceneFastPath()) {
        shared_world_scene::resetWorldSceneRegistry(scratch.worldSceneRegistry);
    }
}

} // namespace game::runtime::session_render_scratch
