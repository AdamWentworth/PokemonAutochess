// src/game/vfx/ClawSwipeVFX.cpp
#include "ClawSwipeVFX.h"

#include <algorithm>

#include "engine/render/Camera3D.h"

void ClawSwipeVFX::update(float dt) {
    particleSupport_.ensureConfigured(cfg);
    particleSupport_.update(dt);
}

void ClawSwipeVFX::render(const Camera3D& camera) {
    particleSupport_.ensureConfigured(cfg);
    particleSupport_.render(camera);
}

void ClawSwipeVFX::emitAt(const glm::vec3& worldPos, const glm::vec3& attackForward, bool metallic) {
    particleSupport_.ensureConfigured(cfg);

    const glm::vec3 fwd =
        game::particle_vfx::shared::SimpleParticleVfxSupport::safeForwardXZ(attackForward);
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), fwd);
    if (glm::length(right) <= 0.0001f) right = glm::vec3(1.0f, 0.0f, 0.0f);
    else right = glm::normalize(right);

    int minP = std::max(0, cfg.minParticles);
    int maxP = std::max(minP, cfg.maxParticles);
    float lifeMin = std::max(0.05f, cfg.minLifeSec);
    float lifeMax = std::max(lifeMin, cfg.maxLifeSec);
    float sizeMin = std::max(0.02f, cfg.minSize);
    float sizeMax = std::max(sizeMin, cfg.maxSize);
    float speedMin = cfg.minSpeed;
    float speedMax = cfg.maxSpeed;
    float lateralSpeed = cfg.lateralSpeed;

    // Scratch should read as a single, explicit 3-line strike (not a blob cluster).
    if (!metallic) {
        minP = 1;
        maxP = 1;
        sizeMin = std::max(sizeMin, 1.22f);
        sizeMax = std::max(sizeMax, 1.46f);
        // Make scratch marks easier to read.
        lifeMin = 0.12f;
        lifeMax = 0.18f;
        speedMin = 0.0f;
        speedMax = 0.0f;
        lateralSpeed = 0.0f;
    }

    const int count = particleSupport_.randInclusive(minP, maxP);
    if (count <= 0) return;

    const float seedMin = metallic ? 0.60f : 0.08f;
    const float seedMax = metallic ? 0.96f : 0.48f;

    for (int i = 0; i < count; ++i) {
        const float side = particleSupport_.randRange(-1.0f, 1.0f);
        const float jitterRadius = metallic ? cfg.spawnRadius : 0.0f;
        const glm::vec3 jitter =
            right * particleSupport_.randRange(-jitterRadius, jitterRadius) +
            glm::vec3(0.0f, particleSupport_.randRange(-jitterRadius, jitterRadius), 0.0f) +
            fwd * particleSupport_.randRange(-jitterRadius * 0.35f, jitterRadius * 0.35f);

        glm::vec3 vel(0.0f);
        if (!metallic) {
            // Tiny intentional drift so the hit does not look fully static.
            vel = fwd * 0.04f
                + right * particleSupport_.randRange(-0.008f, 0.008f)
                + glm::vec3(0.0f, 0.01f, 0.0f);
        } else {
            glm::vec3 dir = fwd + right * side * 0.65f +
                            glm::vec3(0.0f, particleSupport_.randRange(0.05f, 0.30f), 0.0f);
            const float len = glm::length(dir);
            if (len > 0.0001f) dir /= len;
            else dir = fwd;

            const float speed = particleSupport_.randRange(speedMin, speedMax);
            vel = dir * speed + right * particleSupport_.randRange(-lateralSpeed, lateralSpeed);
        }

        ParticleSystem::Particle p;
        p.pos = worldPos + glm::vec3(0.0f, cfg.impactYOffset, 0.0f) + jitter;
        p.vel = vel;
        p.maxLifeSec = particleSupport_.randRange(lifeMin, lifeMax);
        p.lifeSec = p.maxLifeSec;
        p.sizePx = particleSupport_.randRange(sizeMin, sizeMax);
        p.seed = particleSupport_.randRange(seedMin, seedMax);
        if (!metallic) {
            // Cancel system-level acceleration for scratch so it does not float away.
            p.accel = -cfg.acceleration;
        }
        particleSupport_.particles().emit(p);
    }
}
