#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
namespace game::runtime::shared_backend_pose {
namespace {
int resolveSceneAnimIndex(const render_model::MeshData& mesh, const PokemonInstance& unit) {
    int animIndex = unit.activeAnimIndex;
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
        animIndex = unit.currentAttackAnimIndex;
    }
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
        animIndex = unit.animMoveIndex;
    }
    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
        animIndex = unit.animIdleIndex;
    }
    if (animIndex < 0 && !mesh.animations.empty()) {
        animIndex = 0;
    }
    return animIndex;
}

bool isSceneLoopingClipForUnit(const PokemonInstance& unit, int animIndex) {
    if (animIndex < 0) return false;
    if (unit.attackTimerSec > 0.0f) return false;
    if (unit.ledgeJump.active()) return unit.ledgeJump.phase == LedgeJumpPhase::Airborne && animIndex == unit.animJumpLoopIndex;
    if (animIndex == unit.animFaintIndex) return false;
    if (animIndex == unit.animTakeoffIndex ||
        animIndex == unit.animLandIndex ||
        animIndex == unit.animLandAIndex ||
        animIndex == unit.animLandCIndex) {
        return false;
    }
    return animIndex == unit.animIdleIndex ||
           animIndex == unit.animMoveIndex ||
           animIndex == unit.animGroundIdleIndex ||
           animIndex == unit.animAirIdleIndex ||
           animIndex == unit.animTakeoffLoopIndex ||
           animIndex == unit.animLandBIndex;
}

}
bool shouldTreatSceneClipAsLooping(const PokemonInstance& unit, int animIndex) {
    return isSceneLoopingClipForUnit(unit, animIndex);
}

void evaluateScenePose(const render_model::MeshData& mesh,
                       const PokemonInstance& unit,
                       PoseEval& outPose) {
    const int animIndex = resolveSceneAnimIndex(mesh, unit);
    evaluateScenePoseForResolvedClipTime(
        mesh,
        animIndex,
        unit.animTimeSec,
        unit.ledgeJump.active() ? RootMotionPolicy::InPlaceAll : RootMotionPolicy::InPlaceHorizontal,
        isSceneLoopingClipForUnit(unit, animIndex),
        outPose);
}

PoseEval evaluateScenePose(const render_model::MeshData& mesh, const PokemonInstance& unit) {
    PoseEval eval;
    evaluateScenePose(mesh, unit, eval);
    return eval;
}

}
