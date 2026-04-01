#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"

#include "game/runtime/shared/backend/SharedBackendPoseEval.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace game::runtime::shared_projected_scene {

runtime::shared_capture::SnapshotCache makeSharedCaptureSnapshotCache(std::size_t reserveCount) {
    runtime::shared_capture::SnapshotCache cache;
    cache.snaps.reserve(reserveCount);
    cache.byTargetId.reserve(reserveCount);
    return cache;
}

bool appendSharedCaptureAttemptModels(const CaptureModelBridgeArgs& args) {
    shared_capture_model_bridge::Args bridgeArgs{};
    bridgeArgs.gameWorld = args.gameWorld;
    bridgeArgs.supportsWorldIndexedMeshes = args.supportsWorldIndexedMeshes;
    bridgeArgs.hasWorldViewProj = args.hasWorldViewProj;
    bridgeArgs.drawableW = args.drawableW;
    bridgeArgs.drawableH = args.drawableH;
    bridgeArgs.worldCellSize = args.worldCellSize;
    bridgeArgs.worldViewProj = args.worldViewProj;
    bridgeArgs.cameraWorldPos = args.cameraWorldPos;
    bridgeArgs.sharedCaptureAttemptCache = args.sharedCaptureAttemptCache;
    bridgeArgs.renderer = args.renderer;
    bridgeArgs.worldIndexedBatches = args.worldIndexedBatches;
    bridgeArgs.backendTextureByPath = args.backendTextureByPath;
    bridgeArgs.ensureBackendMeshLoaded = args.ensureBackendMeshLoaded;
    bridgeArgs.ensureBackendTextureLoaded = args.ensureBackendTextureLoaded;
    bridgeArgs.evaluateScenePoseForClipTime =
        [](const runtime::render_model::MeshData& mesh, int animIndex, float animTimeSec) {
            return game::runtime::shared_backend_pose::evaluateScenePoseForClipTime(
                mesh, animIndex, animTimeSec);
        };
    return shared_capture_model_bridge::appendSharedCaptureAttemptModels(bridgeArgs);
}

bool appendSharedCaptureAttemptModelsSession(
    GameWorld* gameWorld,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    int drawableW,
    int drawableH,
    float worldCellSize,
    const float* worldViewProj,
    const glm::vec3& cameraWorldPos,
    runtime::shared_capture::SnapshotCache& sharedCaptureAttemptCache,
    IRenderBackend* renderer,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&)>& ensureBackendTextureLoaded) {
    CaptureModelBridgeArgs args{};
    args.gameWorld = gameWorld;
    args.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
    args.hasWorldViewProj = hasWorldViewProj;
    args.drawableW = drawableW;
    args.drawableH = drawableH;
    args.worldCellSize = worldCellSize;
    args.worldViewProj = worldViewProj;
    args.cameraWorldPos = cameraWorldPos;
    args.sharedCaptureAttemptCache = &sharedCaptureAttemptCache;
    args.renderer = renderer;
    args.worldIndexedBatches = &worldIndexedBatches;
    args.backendTextureByPath = &backendTextureByPath;
    args.ensureBackendMeshLoaded = ensureBackendMeshLoaded;
    args.ensureBackendTextureLoaded = ensureBackendTextureLoaded;
    return appendSharedCaptureAttemptModels(args);
}

bool appendSharedCaptureAttemptModelsIfNeededForProjectedWorld(
    IRenderBackend* renderer,
    GameWorld* gameWorld,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    int drawableW,
    int drawableH,
    float worldCellSize,
    const float* worldViewProj,
    const glm::vec3& cameraWorldPos,
    runtime::shared_capture::SnapshotCache& sharedCaptureAttemptCache,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&)>& ensureBackendTextureLoaded) {
    const char* backendId = (renderer ? renderer->backendId() : nullptr);
    if (backendId != nullptr) {
        std::string id(backendId);
        std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (id == "opengl") {
            return false;
        }
    }
    return appendSharedCaptureAttemptModelsSession(
        gameWorld,
        supportsWorldIndexedMeshes,
        hasWorldViewProj,
        drawableW,
        drawableH,
        worldCellSize,
        worldViewProj,
        cameraWorldPos,
        sharedCaptureAttemptCache,
        renderer,
        worldIndexedBatches,
        backendTextureByPath,
        ensureBackendMeshLoaded,
        ensureBackendTextureLoaded);
}

} // namespace game::runtime::shared_projected_scene

