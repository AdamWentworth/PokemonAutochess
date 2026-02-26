#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/PokemonInstance.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/backend_model_cache/BackendModelCache.h"
#include "game/runtime/shared/SharedBackendPoseEval.h"
#include "game/runtime/shared/SharedBackendTextureCache.h"
#include "game/runtime/shared/SharedCapturePresentation.h"
#include "game/runtime/shared/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/shared/SharedTailFireFallbackEmitter.h"
#include "game/runtime/shared/SharedUnitHudBatches.h"
#include "game/runtime/shared/SharedWorldIndexedBatches.h"
#include "game/vfx/TailFireVFX.h"
#include "game/world/GameWorld.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_units {

struct Args {
    const GameDataDb* dataDb = nullptr;
    GameWorld* gameWorld = nullptr;
    float worldCellSize = 1.0f;
    float minDim = 0.0f;
    float boardSurfaceY = 0.0f;
    float lineThickness = 1.0f;
    bool supportsWorldTriangles3D = false;
    bool supportsWorldIndexedMeshes = false;
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
    runtime::shared_capture::SnapshotCache* sharedCaptureAttemptCache = nullptr;
    std::unordered_map<int, shared_tail_fire_fallback::Anchor>* sharedTailFireAnchors = nullptr;
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
    const shared_unit_hud::Config* sharedUnitHudCfg = nullptr;

    std::function<const runtime::backend_model::MeshData*(const PokemonInstance&)> resolveModelMesh;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)> ensureBackendTextureLoaded;
    std::function<std::size_t()> backendModelTriangleLimit;
    std::function<bool()> backendModelFullMeshEnabled;
    std::function<bool()> backendModelFastTexturedPathEnabled;
    std::function<bool()> backendModelBackfaceCullingEnabled;
    std::function<const TailFireVFX::Config&()> getTailFireFallbackCfg;
};

void drawProjectedUnits(const Args& args, const std::vector<PokemonInstance>& units);

} // namespace game::runtime::shared_projected_units
