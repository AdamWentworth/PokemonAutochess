#include "game/runtime/render_prep/BackendProceduralPose.h"

#include <string>

bool test_backend_procedural_pose_contract(std::string& outFail) {
    using game::runtime::backend_anim::computeProceduralPose;
    using game::runtime::backend_anim::deformLocalVertex;
    const glm::vec3 boundsMin(-0.5f, 0.0f, -0.5f);
    const glm::vec3 boundsMax(0.5f, 1.0f, 0.5f);

    {
        PokemonInstance unit;
        unit.isMoving = true;
        unit.animTimeSec = 0.5f;
        const auto pose = computeProceduralPose(unit, 1.0f);
        if (pose.bobY == 0.0f) {
            outFail = "moving units should produce non-zero procedural bob";
            return false;
        }
    }

    {
        PokemonInstance unit;
        unit.attackDurationSec = 0.8f;
        unit.attackTimerSec = 0.6f;
        unit.pendingDamageActive = true;
        const auto pose = computeProceduralPose(unit, 1.0f);
        if (!pose.activeAttackWindow) {
            outFail = "attack state should flag active attack window";
            return false;
        }
        if (pose.attackPulse <= 1.0f && pose.attackLunge == 0.0f) {
            outFail = "attack state should produce procedural lunge/pulse";
            return false;
        }
    }

    {
        PokemonInstance unit;
        unit.fainting = true;
        unit.faintAnimDurationSec = 1.0f;
        unit.faintTimerSec = 0.5f;
        const auto pose = computeProceduralPose(unit, 1.0f);
        if (pose.faintDrop <= 0.0f || pose.faintRoll <= 0.0f) {
            outFail = "fainting state should produce drop and roll offsets";
            return false;
        }
    }

    {
        PokemonInstance unit;
        unit.usesAirLocomotion = true;
        unit.airState = AirLocomotionState::Airborne;
        unit.animTimeSec = 0.3f;
        const auto pose = computeProceduralPose(unit, 1.0f);
        if (pose.bobY <= 0.0f) {
            outFail = "airborne state should elevate procedural bob";
            return false;
        }
    }

    {
        PokemonInstance unit;
        unit.animTimeSec = 0.15f;
        const auto poseA = computeProceduralPose(unit, 1.0f);
        const glm::vec3 v = deformLocalVertex(unit, poseA, glm::vec3(0.1f, 0.8f, 0.2f), boundsMin, boundsMax, 1.0f);
        unit.animTimeSec = 0.65f;
        const auto poseB = computeProceduralPose(unit, 1.0f);
        const glm::vec3 w = deformLocalVertex(unit, poseB, glm::vec3(0.1f, 0.8f, 0.2f), boundsMin, boundsMax, 1.0f);
        if (glm::distance(v, w) < 0.001f) {
            outFail = "procedural vertex deformation should vary over time";
            return false;
        }
    }

    {
        PokemonInstance unit;
        unit.attackDurationSec = 0.8f;
        unit.attackTimerSec = 0.5f;
        unit.pendingDamageActive = true;
        const auto pose = computeProceduralPose(unit, 1.0f);
        const glm::vec3 upper = deformLocalVertex(
            unit,
            pose,
            glm::vec3(0.0f, 0.9f, 0.0f),
            boundsMin,
            boundsMax,
            1.0f);
        if (upper.z <= 0.0f) {
            outFail = "attack deformation should push upper body forward";
            return false;
        }
    }

    return true;
}

