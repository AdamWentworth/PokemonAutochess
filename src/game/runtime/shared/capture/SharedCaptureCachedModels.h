#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/GameWorld.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <functional>
#include <vector>

namespace game::runtime::shared_capture_cached_models {

struct Result {
    bool handled = false;
    bool appendedAny = false;
};

// Non-null deferredBatches queues cached geometry for the caller's scene pass.
// Its borrowed mesh pointers remain valid until this thread prepares another mesh.
Result tryAppend(
    IRenderBackend& renderer,
    bool hasWorldViewProj,
    const float* worldViewProj,
    int drawableW,
    int drawableH,
    const runtime::render_model::MeshData& mesh,
    const std::vector<GameWorld::CaptureAttemptRenderSnapshot>& captureSnaps,
    bool d3d12CapturePrewarmRequested,
    bool treatPokeballAsUntextured,
    bool enableNodeChunkPath,
    const std::function<shared_backend_pose::PoseEval(int animIndex, float animTimeSec)>& evaluateScenePoseForClipTime,
    std::vector<shared_world_batches::WorldIndexedBatch>* deferredBatches);

} // namespace game::runtime::shared_capture_cached_models
