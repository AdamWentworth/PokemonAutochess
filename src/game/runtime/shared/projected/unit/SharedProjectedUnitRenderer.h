#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/PokemonInstance.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/capture/SharedCapturePresentation.h"
#include "game/runtime/shared/projected/core/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/projected/core/SharedProjectedRenderItems.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"
#include "game/runtime/shared/ui/SharedUnitHudBatches.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "game/world/GameWorld.h"

#include <functional>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_units {

namespace detail {

struct CanonicalScenePoseSample {
    float animTimeSec = 0.0f;
    std::uint32_t cacheKey = 0u;
};

CanonicalScenePoseSample canonicalSceneAnimTimeForCacheKey(
    const runtime::render_model::MeshData& mesh,
    int animIndex,
    float animTimeSec,
    float quantizeStepSec);

} // namespace detail

struct PerfStats {
    double totalMs = 0.0;
    double poseEvalMs = 0.0;
    double modelRenderMs = 0.0;
    double modelPrepMs = 0.0;
    double modelGeometryMs = 0.0;
    double overlayMs = 0.0;
    std::uint32_t unitsProcessed = 0u;
    std::uint32_t modelUnits = 0u;
    std::uint32_t clipSkinnedUnits = 0u;
    std::uint32_t sharedRigidBatches = 0u;
    std::uint32_t gpuClipSkinBatches = 0u;
    std::uint32_t gpuClipPaletteBatches = 0u;
    std::uint32_t cpuRewriteBatches = 0u;
    std::uint32_t indexedBatchesQueued = 0u;
};

struct Args {
    IRenderBackend* renderer = nullptr;
    const GameDataDb* dataDb = nullptr;
    GameWorld* gameWorld = nullptr;
    float worldCellSize = 1.0f;
    float minDim = 0.0f;
    float boardSurfaceY = 0.0f;
    float lineThickness = 1.0f;
    bool supportsWorldTriangles3D = false;
    bool supportsWorldIndexedMeshes = false;
    bool characterInkingEnabled = false;
    int graphicsQuality = 3;
    bool enableGpuClipSkinning = false;
    const char* rendererBackendId = nullptr;
    bool hasWorldViewProj = false;
    bool allowPortraitFallback = false;
    bool forcePortraitOverlay = false;
    bool useLegacyGrowlWaveVfx = false;
    bool useLegacyParticleVfxSnapshotBridge = false;
    const float* worldViewProj = nullptr;
    int drawableW = 0;
    int drawableH = 0;
    glm::vec3 cameraWorldPos{0.0f};

    shared_projected_debug::ProjectedDebugVfxBuilder* projectedDebug = nullptr;
    shared_projected_render_items::ProjectedRenderItemRegistry* projectedRenderItems = nullptr;
    shared_world_scene::WorldSceneRegistry* worldSceneRegistry = nullptr;
    IRenderBackend::WorldSceneFrame* worldSceneFrame = nullptr;
    runtime::shared_capture::SnapshotCache* sharedCaptureAttemptCache = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath = nullptr;
    std::vector<shared_projected_scene::DepthTri>* modelDepthTris = nullptr;
    std::vector<shared_projected_scene::DepthWorldTri>* modelDepthWorldTris = nullptr;
    std::size_t* remainingModelTrianglesBudget = nullptr;
    std::vector<IRenderBackend::DebugQuad>* worldQuads = nullptr;
    std::vector<IRenderBackend::DebugLine>* lines = nullptr;
    std::vector<IRenderBackend::DebugLine>* textLines = nullptr;
    std::vector<IRenderBackend::DebugSprite>* sprites = nullptr;
    std::vector<IRenderBackend::DebugTriangle>* worldTriangles = nullptr;
    std::vector<IRenderBackend::WorldTriangle>* world3DTriangles = nullptr;
    std::uint32_t* visibleAnimatedUnitCount = nullptr;
    const shared_unit_hud::Config* sharedUnitHudCfg = nullptr;

    std::function<const runtime::render_model::MeshData*(const PokemonInstance&)> resolveModelMesh;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)> ensureBackendTextureLoaded;
    std::function<std::size_t()> backendModelTriangleLimit;
    std::function<bool()> backendModelFullMeshEnabled;
    std::function<bool()> backendModelFastTexturedPathEnabled;
    std::function<bool()> backendModelBackfaceCullingEnabled;
    PerfStats* perfStats = nullptr;
};

void drawProjectedUnits(const Args& args, const std::vector<PokemonInstance>& units);

} // namespace game::runtime::shared_projected_units

