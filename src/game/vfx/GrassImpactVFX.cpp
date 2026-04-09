// src/game/vfx/GrassImpactVFX.cpp
#include "GrassImpactVFX.h"

#include <algorithm>
#include <cmath>

#include "engine/render/Camera3D.h"

static constexpr float kTwoPi = 6.28318530718f;

void GrassImpactVFX::update(float dt) {
    particleSupport_.ensureConfigured(cfg);
    particleSupport_.update(dt);
}

void GrassImpactVFX::render(const Camera3D& camera) {
    particleSupport_.ensureConfigured(cfg);
    particleSupport_.render(camera);
}

void GrassImpactVFX::emitAt(const glm::vec3& worldPos) {
    particleSupport_.ensureConfigured(cfg);

    int minP = std::max(0, cfg.minParticles);
    int maxP = std::max(minP, cfg.maxParticles);
    int count = particleSupport_.randInclusive(minP, maxP);
    if (count <= 0) return;

    const float lifeMin = std::max(0.05f, cfg.minLifeSec);
    const float lifeMax = std::max(lifeMin, cfg.maxLifeSec);
    const float sizeMin = std::max(0.01f, cfg.minSize);
    const float sizeMax = std::max(sizeMin, cfg.maxSize);
    const float minUp = std::clamp(cfg.minUpward, -1.0f, 1.0f);
    const float maxUp = std::clamp(cfg.maxUpward, -1.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        float yaw = particleSupport_.rand01() * kTwoPi;
        float up = particleSupport_.randRange(minUp, maxUp);
        up = std::clamp(up, -1.0f, 1.0f);
        float lateral = std::sqrt(std::max(0.0f, 1.0f - up * up));

        glm::vec3 dir(std::cos(yaw) * lateral, up, std::sin(yaw) * lateral);

        float speed = particleSupport_.randRange(cfg.minSpeed, cfg.maxSpeed);
        glm::vec3 vel = dir * speed;

        glm::vec3 jitter(
            particleSupport_.randRange(-cfg.spawnRadius, cfg.spawnRadius),
            particleSupport_.randRange(-cfg.spawnRadius, cfg.spawnRadius),
            particleSupport_.randRange(-cfg.spawnRadius, cfg.spawnRadius));

        ParticleSystem::Particle p;
        p.pos = worldPos + glm::vec3(0.0f, cfg.impactYOffset, 0.0f) + jitter;
        p.vel = vel;

        p.maxLifeSec = particleSupport_.randRange(lifeMin, lifeMax);
        p.lifeSec = p.maxLifeSec;
        p.sizePx = particleSupport_.randRange(sizeMin, sizeMax);
        p.seed = particleSupport_.rand01();

        particleSupport_.particles().emit(p);
    }
}
