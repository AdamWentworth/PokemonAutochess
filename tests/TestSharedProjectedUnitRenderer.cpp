#include "game/runtime/shared/projected/SharedProjectedUnitRenderer.h"

#include "engine/runtime/FixedStep.h"

#include <cmath>
#include <string>

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) {
        return true;
    }
    outFail = message;
    return false;
}

} // namespace

bool test_shared_projected_unit_renderer_scene_pose_cache_contract(std::string& outFail) {
    using game::runtime::shared_projected_units::detail::canonicalSceneAnimTimeForCacheKey;

    game::runtime::render_model::MeshData loopedMesh;
    loopedMesh.animations.resize(1u);
    loopedMesh.animations[0].durationSec = 1.0f;

    const auto nearLoopEnd = canonicalSceneAnimTimeForCacheKey(
        loopedMesh,
        0,
        0.999f,
        engine::runtime::fixed_step::kSeconds);
    if (!expect(nearLoopEnd.cacheKey != 0u,
                "Near-loop samples should stay on a valid cached pose instead of snapping to the zero bucket early.",
                outFail)) {
        return false;
    }
    if (!expect(nearLoopEnd.animTimeSec > 0.99f && nearLoopEnd.animTimeSec < 1.0f,
                "Near-loop samples should clamp to the last valid clip time before wrap.",
                outFail)) {
        return false;
    }

    const auto exactLoopWrap = canonicalSceneAnimTimeForCacheKey(
        loopedMesh,
        0,
        1.0f,
        engine::runtime::fixed_step::kSeconds);
    if (!expect(exactLoopWrap.cacheKey == 0u && exactLoopWrap.animTimeSec == 0.0f,
                "Exact loop boundaries should still map to the canonical zero sample.",
                outFail)) {
        return false;
    }

    game::runtime::render_model::MeshData shortMesh;
    shortMesh.animations.resize(1u);
    shortMesh.animations[0].durationSec = 0.01f;
    const auto shortClip = canonicalSceneAnimTimeForCacheKey(
        shortMesh,
        0,
        0.005f,
        engine::runtime::fixed_step::kSeconds);
    if (!expect(shortClip.cacheKey != 0u,
                "Very short clips should bypass coarse quantization instead of collapsing to the zero bucket.",
                outFail)) {
        return false;
    }
    if (!expect(std::abs(shortClip.animTimeSec - 0.005f) < 0.00001f,
                "Very short clips should preserve their wrapped sample time when quantization exceeds clip duration.",
                outFail)) {
        return false;
    }

    return true;
}
