#pragma once

#include <glm/glm.hpp>

#include "engine/render/Model.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"

class Camera3D;

namespace game::preview {

struct PreviewPokemonVisual;

namespace preview_animated_model_presentation {

struct DirectBodySample {
    int animIndex = -1;
    float animTimeSec = 0.0f;
    glm::vec3 renderPos{0.0f};
    glm::mat4 instanceTransform{1.0f};
    Model::AnimatedPose pose;
};

void ensureTailFireSuppressionMask(PreviewPokemonVisual& visual);

bool buildDirectBodySample(const PreviewPokemonVisual& visual,
                           const glm::vec3& worldPos,
                           float yawDeg,
                           DirectBodySample& outSample,
                           float boardSurfaceY = 0.006f);

void drawDirectBody(const Camera3D& camera,
                    PreviewPokemonVisual& visual,
                    const DirectBodySample& sample);

bool buildScenePose(const DirectBodySample& sample,
                    const game::runtime::render_model::MeshData& mesh,
                    game::runtime::shared_backend_pose::PoseEval& outPose);

bool buildScenePose(const PreviewPokemonVisual& visual,
                    const game::runtime::render_model::MeshData& mesh,
                    game::runtime::shared_backend_pose::PoseEval& outPose);

} // namespace preview_animated_model_presentation

} // namespace game::preview
