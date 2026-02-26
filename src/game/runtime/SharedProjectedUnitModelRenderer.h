#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/PokemonInstance.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/backend_model_cache/BackendModelCache.h"
#include "game/runtime/BackendProceduralPose.h"
#include "game/runtime/SharedBackendPoseEval.h"
#include "game/runtime/SharedProjectedDebugVfx.h"
#include "game/runtime/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/SharedTailFireFallbackEmitter.h"
#include "game/runtime/SharedWorldIndexedBatches.h"
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

struct Args {
    const GameDataDb* dataDb = nullptr;
    const PokemonInstance* unit = nullptr;
    const runtime::backend_anim::ProceduralPose* pose = nullptr;
    const runtime::backend_model::MeshData* meshForUnit = nullptr;
    const runtime::shared_backend_pose::PoseEval* scenePose = nullptr;
    bool scenePoseReady = false;
    const IRenderBackend::DebugQuad* tint = nullptr;

    float worldCellSize = 1.0f;
    float boardSurfaceY = 0.0f;
    float unitSize = 0.0f;
    float animPitch = 0.0f;
    float animYaw = 0.0f;
    float animRoll = 0.0f;
    float attackPulse = 1.0f;
    float renderVisualScale = 1.0f;
    float renderCaptureScale = 1.0f;
    float captureVisualTintStrength = 0.0f;
    float modelFadeAlpha = 1.0f;
    glm::vec3 captureTintColor{1.0f};
    glm::vec3 proxyCenter{0.0f};
    glm::vec3 cameraWorldPos{0.0f};

    bool supportsWorldTriangles3D = false;
    bool supportsWorldIndexedMeshes = false;

    shared_projected_debug::ProjectedDebugVfxBuilder* projectedDebug = nullptr;
    std::unordered_map<int, shared_tail_fire_fallback::Anchor>* sharedTailFireAnchors = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
    std::vector<shared_projected_scene::DepthTri>* modelDepthTris = nullptr;
    std::vector<shared_projected_scene::DepthWorldTri>* modelDepthWorldTris = nullptr;
    std::size_t* remainingModelTrianglesBudget = nullptr;
    std::vector<IRenderBackend::WorldTriangle>* world3DTriangles = nullptr;

    std::function<std::size_t()> backendModelTriangleLimit;
    std::function<bool()> backendModelFullMeshEnabled;
    std::function<bool()> backendModelFastTexturedPathEnabled;
    std::function<bool()> backendModelBackfaceCullingEnabled;
};

Result renderProjectedUnitModel(const Args& args);

} // namespace game::runtime::shared_projected_unit_models
