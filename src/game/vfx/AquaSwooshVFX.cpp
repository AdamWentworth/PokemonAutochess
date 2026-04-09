// src/game/vfx/AquaSwooshVFX.cpp
#include "AquaSwooshVFX.h"

#include <algorithm>

#include "engine/render/Camera3D.h"

void AquaSwooshVFX::update(float dt) {
    particleSupport_.ensureConfigured(cfg);
    particleSupport_.update(dt);
}

void AquaSwooshVFX::render(const Camera3D& camera) {
    particleSupport_.ensureConfigured(cfg);
    particleSupport_.render(camera);
}

void AquaSwooshVFX::emitAt(const glm::vec3& worldPos, const glm::vec3& attackForward, Style style) {
    particleSupport_.ensureConfigured(cfg);

    const glm::vec3 fwd =
        game::particle_vfx::shared::SimpleParticleVfxSupport::safeForwardXZ(attackForward);
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), fwd);
    if (glm::length(right) <= 0.0001f) right = glm::vec3(1.0f, 0.0f, 0.0f);
    else right = glm::normalize(right);

    int minP = 8;
    int maxP = 14;
    float minSpeed = 0.9f;
    float maxSpeed = 1.8f;
    float minLife = 0.16f;
    float maxLife = 0.30f;
    float minSize = 0.09f;
    float maxSize = 0.18f;
    float seedMin = 0.05f;
    float seedMax = 0.35f;

    switch (style) {
        case Style::TailWhip:
            minP = 9;
            maxP = 14;
            minSpeed = 0.9f;
            maxSpeed = 1.7f;
            minLife = 0.18f;
            maxLife = 0.34f;
            minSize = 0.10f;
            maxSize = 0.20f;
            seedMin = 0.05f;
            seedMax = 0.35f;
            break;
        case Style::Bubble:
            minP = 12;
            maxP = 18;
            minSpeed = 0.45f;
            maxSpeed = 1.20f;
            minLife = 0.24f;
            maxLife = 0.42f;
            minSize = 0.08f;
            maxSize = 0.17f;
            seedMin = 0.45f;
            seedMax = 0.72f;
            break;
        case Style::WaterGun:
            minP = 14;
            maxP = 22;
            minSpeed = 1.40f;
            maxSpeed = 2.80f;
            minLife = 0.14f;
            maxLife = 0.28f;
            minSize = 0.08f;
            maxSize = 0.16f;
            seedMin = 0.78f;
            seedMax = 0.98f;
            break;
    }

    const int count = particleSupport_.randInclusive(minP, maxP);
    if (count <= 0) return;

    for (int i = 0; i < count; ++i) {
        const float side = particleSupport_.randRange(-1.0f, 1.0f);
        const float up = particleSupport_.randRange(0.00f, 0.45f);
        glm::vec3 dir = fwd + right * side * 0.45f + glm::vec3(0.0f, up, 0.0f);
        const float len = glm::length(dir);
        if (len > 0.0001f) dir /= len;
        else dir = fwd;

        const float speed = particleSupport_.randRange(minSpeed, maxSpeed);
        const glm::vec3 vel = dir * speed;

        ParticleSystem::Particle p;
        p.pos = worldPos + glm::vec3(0.0f, cfg.impactYOffset, 0.0f) +
                right * particleSupport_.randRange(-cfg.spawnRadius, cfg.spawnRadius) +
                fwd * particleSupport_.randRange(-cfg.spawnRadius * 0.5f,
                                                 cfg.spawnRadius * 0.5f);
        p.vel = vel;
        p.maxLifeSec = particleSupport_.randRange(minLife, maxLife);
        p.lifeSec = p.maxLifeSec;
        p.sizePx = particleSupport_.randRange(minSize, maxSize);
        p.seed = particleSupport_.randRange(seedMin, seedMax);
        particleSupport_.particles().emit(p);
    }
}
