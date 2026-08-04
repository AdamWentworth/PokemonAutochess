#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "game/PokemonInstance.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"

namespace game::runtime::shared_backend_pose {

// Source animation clips remain immutable. This policy is applied only while
// evaluating a runtime pose, before the game-owned instance transform is added.
enum class RootMotionPolicy : std::uint8_t {
    PreserveAuthored,
    InPlaceHorizontal,
    InPlaceAll,
};

struct PoseEval {
    bool hasScenePose = false;
    bool hasClipPose = false;
    std::vector<engine::render::model_types::NodeTRS> nodeLocals;
    std::vector<glm::mat4> nodeGlobals;
};

bool shouldTreatSceneClipAsLooping(const PokemonInstance& unit, int animIndex);

void evaluateScenePose(const render_model::MeshData& mesh,
                       const PokemonInstance& unit,
                       PoseEval& outPose);
PoseEval evaluateScenePose(const render_model::MeshData& mesh, const PokemonInstance& unit);
void evaluateScenePoseForResolvedClipTime(const render_model::MeshData& mesh,
                                          int animIndex,
                                          float animTimeSec,
                                          RootMotionPolicy rootMotionPolicy,
                                          PoseEval& outPose);
void evaluateScenePoseForResolvedClipTime(const render_model::MeshData& mesh,
                                          int animIndex,
                                          float animTimeSec,
                                          RootMotionPolicy rootMotionPolicy,
                                          bool loopingClip,
                                          PoseEval& outPose);
PoseEval evaluateScenePoseForResolvedClipTime(const render_model::MeshData& mesh,
                                              int animIndex,
                                              float animTimeSec,
                                              RootMotionPolicy rootMotionPolicy);
PoseEval evaluateScenePoseForResolvedClipTime(const render_model::MeshData& mesh,
                                              int animIndex,
                                              float animTimeSec,
                                              RootMotionPolicy rootMotionPolicy,
                                              bool loopingClip);
void evaluateScenePoseForClipTime(const render_model::MeshData& mesh,
                                  int animIndex,
                                  float animTimeSec,
                                  PoseEval& outPose);
PoseEval evaluateScenePoseForClipTime(const render_model::MeshData& mesh,
                                      int animIndex,
                                      float animTimeSec);

// Applies the controller-owned, always-running loop01 clip over an already
// evaluated body pose. The source material clock is deliberately separate
// from the selected body clip clock.
bool applyContinuousNativeOverlay(
    const render_model::MeshData& mesh,
    float materialTimeSec,
    PoseEval& inOutPose);

} // namespace game::runtime::shared_backend_pose
