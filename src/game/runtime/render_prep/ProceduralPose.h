#pragma once

#include "game/PokemonInstance.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <string>
#include <glm/glm.hpp>

namespace game::runtime::render_prep_pose {

inline bool backendVertexDeformEnabled() {
    static const bool enabled = []() {
        std::string token;
#if defined(_WIN32)
        char* rawValue = nullptr;
        std::size_t rawLength = 0u;
        if (_dupenv_s(&rawValue, &rawLength, "PAC_BACKEND_VERTEX_DEFORM") == 0 &&
            rawValue && rawLength > 0u) {
            token.assign(rawValue);
        }
        if (rawValue) std::free(rawValue);
#else
        if (const char* rawValue = std::getenv("PAC_BACKEND_VERTEX_DEFORM")) {
            token.assign(rawValue);
        }
#endif

        if (token.empty()) return true;
        if (token[0] == '0') return false;

        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (token == "false" || token == "off" || token == "no") return false;
        return true;
    }();
    return enabled;
}

struct ProceduralPose {
    float attackProgress = 0.0f;
    bool activeAttackWindow = false;
    float attackLunge = 0.0f;
    float attackPulse = 1.0f;
    float bobY = 0.0f;
    float faintDrop = 0.0f;
    float faintRoll = 0.0f;
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
};

inline float smooth01(float x) {
    const float t = std::clamp(x, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float remap01(float x, float a, float b) {
    if (std::abs(b - a) <= 1e-6f) return 0.0f;
    return std::clamp((x - a) / (b - a), 0.0f, 1.0f);
}

inline ProceduralPose computeProceduralPose(const PokemonInstance& unit, float worldCellSize) {
    ProceduralPose pose;
    pose.yawDeg = unit.rotation.y;
    pose.pitchDeg = unit.rotation.x;
    pose.rollDeg = unit.rotation.z;

    const float cell = std::max(0.05f, worldCellSize);
    const float moveFreq = unit.isMoving ? 9.4f : 3.2f;
    const float stride = std::sin(unit.animTimeSec * moveFreq);
    const float sway = std::sin(unit.animTimeSec * (unit.isMoving ? 6.4f : 2.4f));
    pose.bobY = stride * (unit.isMoving ? cell * 0.048f : cell * 0.016f);

    pose.yawDeg += sway * (unit.isMoving ? 5.4f : 1.9f);
    pose.pitchDeg += stride * (unit.isMoving ? 7.0f : 2.5f);
    pose.rollDeg += std::sin(unit.animTimeSec * (unit.isMoving ? 8.1f : 2.8f)) * (unit.isMoving ? 4.4f : 1.6f);

    pose.activeAttackWindow =
        unit.attackTimerSec > 0.0f ||
        unit.pendingDamageActive ||
        unit.pendingImpactActive ||
        unit.pendingProjectileActive;
    const float attackDuration = std::max(0.05f, unit.attackDurationSec);
    pose.attackProgress = std::clamp(unit.attackTimerSec / attackDuration, 0.0f, 1.0f);
    const float attackT = 1.0f - pose.attackProgress;
    if (pose.activeAttackWindow) {
        const float windup = smooth01(remap01(attackT, 0.00f, 0.22f)) * (1.0f - smooth01(remap01(attackT, 0.22f, 0.50f)));
        const float strike = smooth01(remap01(attackT, 0.26f, 0.46f)) * (1.0f - smooth01(remap01(attackT, 0.46f, 0.72f)));
        const float recover = smooth01(remap01(attackT, 0.64f, 0.96f));

        pose.attackLunge = cell * (-0.05f * windup + 0.16f * strike + 0.03f * recover);
        pose.attackPulse = 1.0f + 0.09f * strike;
        pose.pitchDeg += (-16.0f * windup) + (32.0f * strike) - (8.0f * recover);
        pose.rollDeg += (unit.side == PokemonSide::Player ? -1.0f : 1.0f) *
            ((5.0f * windup) - (8.0f * strike) + (2.5f * recover));
    }

    const float faintProgress = (unit.fainting && unit.faintAnimDurationSec > 0.0f)
        ? std::clamp(unit.faintTimerSec / unit.faintAnimDurationSec, 0.0f, 1.0f)
        : 0.0f;
    pose.faintDrop = faintProgress * cell * 0.40f;
    pose.faintRoll = faintProgress * 85.0f;

    if (unit.usesAirLocomotion) {
        switch (unit.airState) {
        case AirLocomotionState::TakingOff: {
            const float takeoffSec = std::max(0.20f, unit.takeoffSec);
            const float t = smooth01(unit.airStateTimeSec / takeoffSec);
            pose.bobY += t * cell * 0.18f;
            break;
        }
        case AirLocomotionState::Airborne:
            pose.bobY += cell * 0.20f + std::sin(unit.animTimeSec * 12.0f) * cell * 0.05f;
            pose.rollDeg += std::sin(unit.animTimeSec * 18.0f) * 6.0f;
            break;
        case AirLocomotionState::LandingStart:
        case AirLocomotionState::LandingLoop:
        case AirLocomotionState::LandingFinish:
            pose.bobY += std::max(0.0f, unit.visualYOffset) * 0.65f;
            pose.pitchDeg += 6.0f;
            break;
        case AirLocomotionState::Grounded:
        default:
            break;
        }
    }

    return pose;
}

inline glm::vec3 deformLocalVertex(const PokemonInstance& unit,
                                   const ProceduralPose& pose,
                                   const glm::vec3& localPos,
                                   const glm::vec3& boundsMin,
                                   const glm::vec3& boundsMax,
                                   float worldCellSize) {
    if (!backendVertexDeformEnabled()) return localPos;

    glm::vec3 p = localPos;
    const glm::vec3 boundsSpan = glm::max(boundsMax - boundsMin, glm::vec3(0.001f));
    const glm::vec3 boundsCenter = (boundsMin + boundsMax) * 0.5f;

    const float xSpan = std::max(0.001f, boundsSpan.x * 0.5f);
    const float zSpan = std::max(0.001f, boundsSpan.z * 0.5f);
    const float y01 = std::clamp((p.y - boundsMin.y) / std::max(0.001f, boundsSpan.y), 0.0f, 1.0f);
    const float xNorm = std::clamp((p.x - boundsCenter.x) / xSpan, -1.0f, 1.0f);
    const float zNorm = std::clamp((p.z - boundsCenter.z) / zSpan, -1.0f, 1.0f);
    const float torso = 0.28f + 0.72f * y01;
    const float lowerBody = 1.0f - y01;

    const float cell = std::max(0.05f, worldCellSize);
    const float locomotionFreq = unit.isMoving ? 9.8f : 4.2f;
    const float locomotionPhase = unit.animTimeSec * locomotionFreq + zNorm * 2.1f;
    const float swayWave = std::sin(locomotionPhase);
    const float swayAmp = cell * (unit.isMoving ? 0.055f : 0.020f) * torso;
    p.x += swayWave * swayAmp;
    p.z += std::sin(unit.animTimeSec * (unit.isMoving ? 7.5f : 3.4f) + xNorm * 2.0f) * swayAmp * 0.35f;

    const float bounceWave = std::cos(unit.animTimeSec * (unit.isMoving ? 8.6f : 3.0f) + xNorm * 2.6f);
    const float bounceAmp = cell * (unit.isMoving ? 0.022f : 0.010f);
    p.y += bounceWave * bounceAmp * lowerBody;

    const float breathWave = std::sin(unit.animTimeSec * 2.2f + zNorm * 1.8f);
    p.x += breathWave * cell * 0.008f * torso;
    p.z += breathWave * cell * 0.012f * torso;

    if (pose.activeAttackWindow) {
        const float attackT = 1.0f - pose.attackProgress;
        const float windup = smooth01(remap01(attackT, 0.00f, 0.24f)) * (1.0f - smooth01(remap01(attackT, 0.24f, 0.52f)));
        const float strike = smooth01(remap01(attackT, 0.22f, 0.56f)) * (1.0f - smooth01(remap01(attackT, 0.56f, 0.88f)));
        const float recover = smooth01(remap01(attackT, 0.70f, 1.00f));

        p.z += cell * (0.11f * windup + 0.22f * strike - 0.06f * recover) * torso;
        p.y -= cell * (0.06f * windup + 0.04f * strike) * lowerBody;
        p.x += xNorm * cell * (0.018f * strike);
    }

    if (pose.faintDrop > 0.0f) {
        const float faintT = std::clamp(pose.faintDrop / (cell * 0.40f), 0.0f, 1.0f);
        p.y -= faintT * cell * 0.26f * torso;
        p.x += xNorm * faintT * cell * 0.05f;
        p.z += zNorm * faintT * cell * 0.03f;
    }

    if (unit.usesAirLocomotion) {
        p.y += std::sin(unit.animTimeSec * 13.0f + zNorm * 4.0f) * cell * 0.012f * torso;
        p.x += std::sin(unit.animTimeSec * 17.0f + y01 * 6.0f) * cell * 0.010f * torso;
    }

    return p;
}

} // namespace game::runtime::render_prep_pose

