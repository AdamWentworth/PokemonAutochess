#include "game/runtime/session/SessionRenderScratch.h"

namespace game::runtime::session_render_scratch {

bool ProjectedBackdropCacheKey::operator==(const ProjectedBackdropCacheKey& other) const {
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
           line == other.line;
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
    scratch.projectedBackdropLinesCount = 0u;
}

void beginFrame(RenderScratch& scratch, bool useProjectedWorldLayout) {
    shared_projected_render_items::beginProjectedRenderItemsFrame(
        scratch.projectedRenderItems);
    scratch.worldQuads.clear();
    scratch.worldIndexedBatches.clear();
    scratch.overlayQuads.clear();
    scratch.textLines.clear();
    scratch.sprites.clear();
    scratch.unitLabels.clear();
    scratch.sharedTailFireAnchors.clear();
    scratch.sharedCaptureAttemptCache.snaps.clear();
    scratch.sharedCaptureAttemptCache.byTargetId.clear();

    if (useProjectedWorldLayout && scratch.projectedBackdropValid) {
        scratch.worldBackgroundQuads.resize(scratch.projectedBackdropWorldBackgroundQuadsCount);
        scratch.worldTriangles.resize(scratch.projectedBackdropWorldTrianglesCount);
        scratch.world3DTriangles.resize(scratch.projectedBackdropWorld3DTrianglesCount);
        scratch.lines.resize(scratch.projectedBackdropLinesCount);
        return;
    }

    scratch.worldBackgroundQuads.clear();
    scratch.worldTriangles.clear();
    scratch.world3DTriangles.clear();
    scratch.lines.clear();
    if (!useProjectedWorldLayout) {
        invalidateProjectedBackdrop(scratch);
        shared_projected_render_items::resetProjectedRenderItems(
            scratch.projectedRenderItems);
    }
}

} // namespace game::runtime::session_render_scratch
