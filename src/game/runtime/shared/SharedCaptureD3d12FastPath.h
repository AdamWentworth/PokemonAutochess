#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/GameWorld.h"
#include "game/runtime/backend_model_cache/BackendModelCache.h"
#include "game/runtime/shared/SharedBackendPoseEval.h"

#include <functional>
#include <vector>

namespace game::runtime::shared_capture_d3d12_fast {

struct Result {
    bool handled = false;
    bool appendedAny = false;
};

Result tryAppend(
    IRenderBackend& renderer,
    bool hasWorldViewProj,
    const float* worldViewProj,
    int drawableW,
    int drawableH,
    const runtime::backend_model::MeshData& mesh,
    const std::vector<GameWorld::CaptureAttemptRenderSnapshot>& captureSnaps,
    bool d3d12CapturePrewarmRequested,
    bool treatPokeballAsUntextured,
    bool enableNodeChunkPath,
    const std::function<shared_backend_pose::PoseEval(int animIndex, float animTimeSec)>& evaluateScenePoseForClipTime);

} // namespace game::runtime::shared_capture_d3d12_fast
