#pragma once
#include "game/PokemonInstance.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "pokemon/models/PoseCompatibility.h"
namespace game::runtime::shared_backend_pose {
using pokemon::models::pose::PoseEval;
using pokemon::models::pose::RootMotionPolicy;
using pokemon::models::pose::evaluateScenePoseForResolvedClipTime;
using pokemon::models::pose::evaluateScenePoseForClipTime;
using pokemon::models::pose::isTongueSurfaceConcealed;
using pokemon::models::pose::continuousNativeOverlayAnimationIndex;
using pokemon::models::pose::animationOwnsNativeEffectVisibility;
using pokemon::models::pose::applyContinuousNativeOverlay;
bool shouldTreatSceneClipAsLooping(const PokemonInstance& unit, int animIndex);
void evaluateScenePose(const render_model::MeshData& mesh,
    const PokemonInstance& unit, PoseEval& outPose);
PoseEval evaluateScenePose(const render_model::MeshData& mesh, const PokemonInstance& unit);
}
