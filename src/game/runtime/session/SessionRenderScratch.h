#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/runtime/shared/capture/SharedCapturePresentation.h"
#include "game/runtime/shared/projected/core/SharedProjectedRenderItems.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

namespace game::runtime::route1_environment {
class RuntimeEnvironment;
}

namespace game::runtime::session_render_scratch {

struct BackendUnitLabel {
    float x = 0.0f;
    float y = 0.0f;
    std::string text;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
};

struct ProjectedBackdropCacheKey {
    bool supportsWorldTriangles3D = false;
    bool supportsWorldIndexedMeshes = false;
    bool enableBackdropTiles = true;
    int rows = 0;
    int cols = 0;
    int benchSlots = 0;
    int benchGapCells = 0;
    int graphicsQuality = 3;
    float worldCellSize = 0.0f;
    float boardMinX = 0.0f;
    float boardMinZ = 0.0f;
    float boardMaxX = 0.0f;
    float boardMaxZ = 0.0f;
    int drawableW = 0;
    int drawableH = 0;
    float boardX = 0.0f;
    float boardY = 0.0f;
    float boardW = 0.0f;
    float boardH = 0.0f;
    float cellW = 0.0f;
    float cellH = 0.0f;
    float line = 0.0f;
    int arenaBackdropTheme = 0;
    float route1BackdropScaleMul = 0.0f;
    float route1BackdropOffsetXCells = 0.0f;
    float route1BackdropOffsetY = 0.0f;
    float route1BackdropOffsetZCells = 0.0f;
    float route1BackdropYawDeg = 0.0f;
    bool canonicalRoute1Environment = false;

    bool operator==(const ProjectedBackdropCacheKey& other) const;
};

struct RenderScratch {
    std::vector<IRenderBackend::DebugQuad> worldBackgroundQuads;
    std::vector<IRenderBackend::DebugQuad> worldQuads;
    std::vector<IRenderBackend::DebugTriangle> worldTriangles;
    std::vector<IRenderBackend::WorldTriangle> world3DTriangles;
    std::vector<shared_world_batches::WorldIndexedBatch> worldIndexedBatches;
    std::vector<IRenderBackend::DebugQuad> overlayQuads;
    std::vector<IRenderBackend::DebugLine> lines;
    std::vector<IRenderBackend::DebugLine> textLines;
    std::vector<IRenderBackend::DebugSprite> sprites;
    std::vector<BackendUnitLabel> unitLabels;
    shared_projected_render_items::ProjectedRenderItemRegistry projectedRenderItems;
    shared_world_scene::WorldSceneRegistry worldSceneRegistry;
    IRenderBackend::WorldSceneFrame worldSceneFrame;
    shared_capture::SnapshotCache sharedCaptureAttemptCache;
    std::uint32_t lastGraphicsQualityGeneration = 0u;
    std::shared_ptr<route1_environment::RuntimeEnvironment>
        route1RuntimeEnvironment;
    bool route1RuntimeLoadAttempted = false;
    std::string route1RuntimeSceneId;
    std::string route1RuntimeLoadError;
    bool projectedBackdropValid = false;
    ProjectedBackdropCacheKey projectedBackdropKey{};
    std::size_t projectedBackdropWorldBackgroundQuadsCount = 0u;
    std::size_t projectedBackdropWorldTrianglesCount = 0u;
    std::size_t projectedBackdropWorld3DTrianglesCount = 0u;
    std::size_t projectedBackdropWorldIndexedBatchesCount = 0u;
    std::size_t projectedBackdropLinesCount = 0u;
};

RenderScratch& threadScratch();
void ensureCapacity(RenderScratch& scratch);
void invalidateProjectedBackdrop(RenderScratch& scratch);
void resetSceneCaches(RenderScratch& scratch);
void resetForContentReload(RenderScratch& scratch);
void beginFrame(RenderScratch& scratch,
                bool useProjectedWorldLayout,
                IRenderBackend* renderer = nullptr);

} // namespace game::runtime::session_render_scratch

