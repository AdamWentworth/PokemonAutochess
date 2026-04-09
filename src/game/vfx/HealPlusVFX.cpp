// src/game/vfx/HealPlusVFX.cpp
#include "HealPlusVFX.h"

#include <algorithm>
#include <cmath>

#include "engine/render/Camera3D.h"

static constexpr float kTwoPi = 6.28318530718f;

void HealPlusVFX::update(float dt) {
    particleSupport_.ensureConfigured(cfg);
    particleSupport_.update(dt);
}

void HealPlusVFX::render(const Camera3D& camera) {
    particleSupport_.ensureConfigured(cfg);
    particleSupport_.render(camera);
}

void HealPlusVFX::emitAt(const glm::vec3& worldPos) {
    particleSupport_.ensureConfigured(cfg);

    int minP = std::max(0, cfg.minParticles);
    int maxP = std::max(minP, cfg.maxParticles);
    int count = particleSupport_.randInclusive(minP, maxP);
    if (count <= 0) return;

    for (int i = 0; i < count; ++i) {
        float ang = particleSupport_.rand01() * kTwoPi;
        float r = particleSupport_.randRange(0.0f, cfg.radius);
        float h = particleSupport_.randRange(cfg.minHeight, cfg.maxHeight);

        glm::vec3 offset(std::cos(ang) * r, h, std::sin(ang) * r);

        float spd = particleSupport_.randRange(cfg.minSpeed, cfg.maxSpeed);
        glm::vec3 vel(
            std::cos(ang) * spd * 0.18f,
            spd * 0.25f,
            std::sin(ang) * spd * 0.18f);

        ParticleSystem::Particle p;
        p.pos = worldPos + offset;
        p.vel = vel;
        p.accel = glm::vec3(0.0f, 0.55f, 0.0f);
        p.maxLifeSec = particleSupport_.randRange(cfg.minLifeSec, cfg.maxLifeSec);
        p.lifeSec = p.maxLifeSec;
        p.sizePx = particleSupport_.randRange(cfg.minSize, cfg.maxSize);
        p.seed = particleSupport_.rand01();

        particleSupport_.particles().emit(p);
    }
}
