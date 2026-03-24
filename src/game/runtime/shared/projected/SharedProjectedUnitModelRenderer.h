#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/PokemonInstance.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/render_prep/ProceduralPose.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/projected/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/projected/SharedProjectedRenderItems.h"
#include "game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "game/vfx/TailFireVFX.h"

#include <functional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_unit_models {

struct Result {
    bool skipUnit = false;
    bool drewModelMesh = false;
};

struct PerfBreakdown {
    double prepMs = 0.0;
    double geometryMs = 0.0;
    std::uint32_t sharedRigidBatches = 0u;
    std::uint32_t gpuClipSkinBatches = 0u;
    std::uint32_t gpuClipPaletteBatches = 0u;
    std::uint32_t cpuRewriteBatches = 0u;
    std::uint32_t indexedBatchesQueued = 0u;
};

struct Args {
    IRenderBackend* renderer = nullptr;
    const GameDataDb* dataDb = nullptr;
    const PokemonInstance* unit = nullptr;
    const runtime::render_prep_pose::ProceduralPose* pose = nullptr;
    const runtime::render_model::MeshData* meshForUnit = nullptr;
    const runtime::shared_backend_pose::PoseEval* scenePose = nullptr;
    const char* backendId = nullptr;
    bool scenePoseReady = false;
    bool enableClipSkinning = true;
    bool enableGpuClipSkinning = false;
    const IRenderBackend::DebugQuad* tint = nullptr;

    float worldCellSize = 1.0f;
    float boardSurfaceY = 0.0f;
    float unitSize = 0.0f;
    float animPitch = 0.0f;
    float animYaw = 0.0f;
    float animRoll = 0.0f;
    float attackPulse = 1.0f;
    float materialTimeSec = 0.0f;
    float renderVisualScale = 1.0f;
    float renderCaptureScale = 1.0f;
    float captureVisualTintStrength = 0.0f;
    float modelFadeAlpha = 1.0f;
    glm::vec3 captureTintColor{1.0f};
    glm::vec3 proxyCenter{0.0f};
    glm::vec3 cameraWorldPos{0.0f};

    bool supportsWorldTriangles3D = false;
    bool supportsWorldIndexedMeshes = false;
    bool characterInkingEnabled = false;
    int graphicsQuality = 3;

    shared_projected_debug::ProjectedDebugVfxBuilder* projectedDebug = nullptr;
    shared_projected_render_items::ProjectedRenderItemRegistry* projectedRenderItems = nullptr;
    shared_world_scene::WorldSceneRegistry* worldSceneRegistry = nullptr;
    IRenderBackend::WorldSceneFrame* worldSceneFrame = nullptr;
    std::unordered_map<int, shared_tail_fire_fallback::Anchor>* sharedTailFireAnchors = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath = nullptr;
    std::vector<shared_projected_scene::DepthTri>* modelDepthTris = nullptr;
    std::vector<shared_projected_scene::DepthWorldTri>* modelDepthWorldTris = nullptr;
    std::size_t* remainingModelTrianglesBudget = nullptr;
    std::vector<IRenderBackend::WorldTriangle>* world3DTriangles = nullptr;

    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)> ensureBackendTextureLoaded;
    std::function<std::size_t()> backendModelTriangleLimit;
    std::function<bool()> backendModelFullMeshEnabled;
    std::function<bool()> backendModelFastTexturedPathEnabled;
    std::function<bool()> backendModelBackfaceCullingEnabled;
    PerfBreakdown* perfBreakdown = nullptr;
};

Result renderProjectedUnitModel(const Args& args);

} // namespace game::runtime::shared_projected_unit_models



