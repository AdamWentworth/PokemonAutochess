#include "game/runtime/session/SessionRenderScratch.h"

#include <string>

bool test_session_render_scratch_contract(std::string& outFail) {
    using game::runtime::session_render_scratch::ProjectedBackdropCacheKey;
    using game::runtime::session_render_scratch::RenderScratch;

    {
        ProjectedBackdropCacheKey a;
        ProjectedBackdropCacheKey b;
        a.cols = 8;
        b.cols = 8;
        if (!(a == b)) {
            outFail = "SessionRenderScratch should treat identical projected backdrop keys as equal.";
            return false;
        }
        b.line = 2.0f;
        if (a == b) {
            outFail = "SessionRenderScratch should detect projected backdrop key changes.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        game::runtime::session_render_scratch::ensureCapacity(scratch);
        if (scratch.worldBackgroundQuads.capacity() < 1024u ||
            scratch.world3DTriangles.capacity() < 120000u ||
            scratch.sharedTailFireAnchors.bucket_count() < 16u) {
            outFail = "SessionRenderScratch should reserve the expected baseline scratch capacities.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        scratch.projectedBackdropValid = true;
        scratch.projectedBackdropWorldBackgroundQuadsCount = 2u;
        scratch.projectedBackdropWorldTrianglesCount = 3u;
        scratch.projectedBackdropWorld3DTrianglesCount = 4u;
        scratch.projectedBackdropWorldIndexedBatchesCount = 2u;
        scratch.projectedBackdropLinesCount = 5u;
        scratch.worldBackgroundQuads.resize(10u);
        scratch.worldTriangles.resize(10u);
        scratch.world3DTriangles.resize(10u);
        scratch.worldIndexedBatches.resize(4u);
        scratch.worldIndexedBatches[0].geometryCacheKey = "tree_a";
        scratch.worldIndexedBatches[1].geometryCacheKey = "tree_b";
        scratch.lines.resize(10u);
        scratch.worldQuads.resize(3u);
        scratch.overlayQuads.resize(3u);
        scratch.unitLabels.resize(3u);

        game::runtime::session_render_scratch::beginFrame(scratch, true);
        if (scratch.worldBackgroundQuads.size() != 2u ||
            scratch.worldTriangles.size() != 3u ||
            scratch.world3DTriangles.size() != 4u ||
            scratch.worldIndexedBatches.size() != 2u ||
            scratch.worldIndexedBatches[0].geometryCacheKey != "tree_a" ||
            scratch.worldIndexedBatches[1].geometryCacheKey != "tree_b" ||
            scratch.lines.size() != 5u ||
            !scratch.projectedBackdropValid ||
            !scratch.worldQuads.empty() ||
            !scratch.overlayQuads.empty() ||
            !scratch.unitLabels.empty()) {
            outFail = "SessionRenderScratch should retain cached projected backdrop buffers and indexed batches while clearing per-frame overlay data.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        scratch.projectedBackdropValid = true;
        scratch.projectedBackdropWorldBackgroundQuadsCount = 2u;
        scratch.worldBackgroundQuads.resize(2u);

        game::runtime::session_render_scratch::beginFrame(scratch, false);
        if (scratch.projectedBackdropValid ||
            scratch.projectedBackdropWorldBackgroundQuadsCount != 0u ||
            !scratch.worldBackgroundQuads.empty()) {
            outFail = "SessionRenderScratch should invalidate projected backdrop cache when projected layout is not active.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        scratch.worldSceneRegistry.geometries.push_back(IRenderBackend::WorldSceneGeometry{});
        scratch.worldSceneRegistry.materials.push_back(IRenderBackend::WorldSceneMaterial{});
        scratch.worldSceneRegistry.renderObjects.push_back(IRenderBackend::WorldSceneRenderObject{});
        scratch.projectedRenderItems.entries.emplace(
            game::runtime::shared_projected_render_items::ProjectedRenderItemKey{},
            game::runtime::shared_projected_render_items::ProjectedRenderItemEntry{});
        scratch.worldSceneFrame.drawClasses.push_back(IRenderBackend::WorldSceneDrawClass{});
        const std::uint32_t previousGeneration = scratch.worldSceneRegistry.generation;

        game::runtime::session_render_scratch::resetSceneCaches(scratch);
        if (!scratch.worldSceneRegistry.geometries.empty() ||
            !scratch.worldSceneRegistry.materials.empty() ||
            !scratch.worldSceneRegistry.renderObjects.empty() ||
            !scratch.projectedRenderItems.entries.empty() ||
            !scratch.worldSceneFrame.drawClasses.empty() ||
            scratch.worldSceneRegistry.generation == previousGeneration) {
            outFail = "SessionRenderScratch should hard-reset persistent scene caches together.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        scratch.worldSceneRegistry.generation = 41u;
        scratch.worldSceneRegistry.materials.push_back(
            IRenderBackend::WorldSceneMaterial{});
        scratch.worldIndexedBatches.resize(2u);

        game::runtime::session_render_scratch::resetForContentReload(
            scratch);
        if (!scratch.worldSceneRegistry.materials.empty() ||
            !scratch.worldIndexedBatches.empty() ||
            scratch.worldSceneRegistry.generation != 42u) {
            outFail =
                "Content reload should clear all render scratch while preserving a monotonically increasing world-scene generation.";
            return false;
        }

        game::runtime::session_render_scratch::resetForContentReload(
            scratch);
        if (scratch.worldSceneRegistry.generation != 43u) {
            outFail =
                "Repeated content reloads should not recycle a prior world-scene generation.";
            return false;
        }
    }

    return true;
}
