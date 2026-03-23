#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "game/PokemonInstance.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"

namespace game::runtime::shared_backend_pose {

struct PoseEval {
    bool hasScenePose = false;
    bool hasClipPose = false;
    std::vector<pac_model_types::NodeTRS> nodeLocals;
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
                                          bool preserveRootMotionCarrierXZ,
                                          PoseEval& outPose);
void evaluateScenePoseForResolvedClipTime(const render_model::MeshData& mesh,
                                          int animIndex,
                                          float animTimeSec,
                                          bool preserveRootMotionCarrierXZ,
                                          bool loopingClip,
                                          PoseEval& outPose);
PoseEval evaluateScenePoseForResolvedClipTime(const render_model::MeshData& mesh,
                                              int animIndex,
                                              float animTimeSec,
                                              bool preserveRootMotionCarrierXZ);
PoseEval evaluateScenePoseForResolvedClipTime(const render_model::MeshData& mesh,
                                              int animIndex,
                                              float animTimeSec,
                                              bool preserveRootMotionCarrierXZ,
                                              bool loopingClip);
void evaluateScenePoseForClipTime(const render_model::MeshData& mesh,
                                  int animIndex,
                                  float animTimeSec,
                                  PoseEval& outPose);
PoseEval evaluateScenePoseForClipTime(const render_model::MeshData& mesh,
                                      int animIndex,
                                      float animTimeSec);

} // namespace game::runtime::shared_backend_pose
