#pragma once

#include <algorithm>
#include <cmath>
#include <span>
#include <glm/glm.hpp>

namespace game::render::encounter_grass_motion {

// Contact is evaluated in source centimetres. Only visible, grounded actors
// reach this presentation layer; it must never query hidden gameplay units.
struct Contact {
    glm::vec3 position{};
    glm::vec3 motion{};
    float strength = 1;
    float motionStrength = 1;
};

struct State {
    glm::vec2 bend{}; // horizontal X/Z bend, in radians
    glm::vec2 velocity{};
};

inline glm::vec2 target(glm::vec3 clusterGround, std::span<const Contact> contacts,
                        float seconds, float phase) {
    constexpr float radiusCm = 95;
    constexpr float bodyRadiusCm = 25;
    constexpr float maximumBend = 0.72f;
    glm::vec2 bend(0);
    float totalInfluence = 0;
    for (const auto &contact : contacts) {
        // A Pokemon below a half-metre ledge cannot push the grass above it.
        if (std::abs(clusterGround.y - contact.position.y) > 35) continue;
        const glm::vec2 delta(clusterGround.x - contact.position.x, clusterGround.z - contact.position.z);
        const float distance = glm::length(delta);
        if (distance >= radiusCm) continue;
        const float proximity = std::clamp((radiusCm - distance) / (radiusCm - bodyRadiusCm), 0.0f, 1.0f);
        const float influence = proximity * proximity * (3 - 2 * proximity) * contact.strength;
        const glm::vec2 motion(contact.motion.x, contact.motion.z);
        glm::vec2 direction = distance > 0.001f ? delta / distance : motion;
        if (glm::length(direction) < 0.001f) direction = {std::cos(phase), std::sin(phase)};
        direction += motion * (0.22f * contact.motionStrength);
        direction = glm::normalize(direction);
        const glm::vec2 sideways(-direction.y, direction.x);
        // Movement flicks the blades sideways as well as opening them. At rest
        // pressure remains, while this fast component stops completely.
        const float flutter = std::sin(seconds * 23 + phase) * 0.14f * contact.motionStrength;
        bend += influence * (direction + sideways * flutter);
        totalInfluence += influence;
    }
    // Blend overlapping contacts continuously; don't switch ownership at the
    // midpoint between two Pokemon or flatten the entire bed in a crowd.
    return maximumBend * bend / std::max(1.0f, totalInfluence);
}

inline void advance(State &state, glm::vec2 targetBend, float dt) {
    if (dt <= 0) return;
    // Exact damped-spring step for a held target: stable at different frame
    // rates, with a short rebound after release instead of a mechanical fade.
    constexpr float frequency = 19;
    constexpr float damping = 0.55f;
    constexpr float decay = frequency * damping;
    const float oscillation = frequency * std::sqrt(1 - damping * damping);
    const float cosine = std::cos(oscillation * dt);
    const float sine = std::sin(oscillation * dt);
    const float envelope = std::exp(-decay * dt);
    const glm::vec2 offset = state.bend - targetBend;
    const glm::vec2 coefficient = (state.velocity + decay * offset) / oscillation;
    state.bend = targetBend + envelope * (offset * cosine + coefficient * sine);
    state.velocity = envelope * (state.velocity * cosine -
                                 (decay * state.velocity + frequency * frequency * offset) * (sine / oscillation));
}

inline glm::mat4 deformation(glm::vec2 bend) {
    const float angle = glm::length(bend);
    glm::mat4 result(1);
    if (angle < 0.000001f) return result;
    // Bend the tops around the ground plane, leaving every point at Y=0
    // fixed. The source wind pivots sit near the blade tops, and rotating
    // contact around those pivots barely parts the tips and lifts the roots.
    const glm::vec2 lean = bend * (std::sin(angle) / angle);
    result[1][0] = lean.x;
    result[1][2] = lean.y;
    result[1][1] = std::cos(angle);
    return result;
}

} // namespace game::render::encounter_grass_motion
