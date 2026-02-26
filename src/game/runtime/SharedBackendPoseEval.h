#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "game/PokemonInstance.h"
#include "game/runtime/backend_model_cache/BackendModelCache.h"

namespace game::runtime::shared_backend_pose {

struct PoseEval {
    bool hasScenePose = false;
    bool hasClipPose = false;
    std::vector<pac_model_types::NodeTRS> nodeLocals;
    std::vector<glm::mat4> nodeGlobals;
};

PoseEval evaluateScenePose(const backend_model::MeshData& mesh, const PokemonInstance& unit);
PoseEval evaluateScenePoseForClipTime(const backend_model::MeshData& mesh,
                                      int animIndex,
                                      float animTimeSec);

} // namespace game::runtime::shared_backend_pose
