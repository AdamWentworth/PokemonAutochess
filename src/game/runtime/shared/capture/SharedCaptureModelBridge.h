#pragma once

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/capture/SharedCapturePresentation.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

class GameWorld;

namespace game::runtime::shared_capture_model_bridge {

struct Args {
    GameWorld* gameWorld = nullptr;
    bool supportsWorldIndexedMeshes = false;
    bool hasWorldViewProj = false;
    int drawableW = 0;
    int drawableH = 0;
    float worldCellSize = 1.0f;
    const float* worldViewProj = nullptr;
    glm::vec3 cameraWorldPos{0.0f};
    runtime::shared_capture::SnapshotCache* sharedCaptureAttemptCache = nullptr;
    IRenderBackend* renderer = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath = nullptr;
    std::function<runtime::render_model::MeshData*(const std::string&)> ensureBackendMeshLoaded;
    std::function<SharedBackendTextureCacheEntry*(const std::string&)> ensureBackendTextureLoaded;
    std::function<shared_backend_pose::PoseEval(const runtime::render_model::MeshData&, int, float)>
        evaluateScenePoseForClipTime;
};

bool appendSharedCaptureAttemptModels(const Args& args);

} // namespace game::runtime::shared_capture_model_bridge
