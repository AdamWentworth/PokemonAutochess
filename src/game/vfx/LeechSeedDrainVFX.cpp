// src/game/vfx/LeechSeedDrainVFX.cpp
#include "LeechSeedDrainVFX.h"

#include <algorithm>
#include <cmath>

#include "engine/render/Camera3D.h"

void LeechSeedDrainVFX::update(float dt) {
    particleSupport_.ensureConfigured(cfg);
    particleSupport_.update(dt);
}

void LeechSeedDrainVFX::render(const Camera3D& camera) {
    particleSupport_.ensureConfigured(cfg);
    particleSupport_.render(camera);
}

void LeechSeedDrainVFX::emitBetween(const glm::vec3& startPos,
                                    const glm::vec3& endPos,
                                    float travelSec) {
    particleSupport_.ensureConfigured(cfg);

    const float T = std::max(0.10f, travelSec);

    int minP = std::max(0, cfg.minParticles);
    int maxP = std::max(minP, cfg.maxParticles);
    int count = particleSupport_.randInclusive(minP, maxP);
    if (count <= 0) return;

    for (int i = 0; i < count; ++i) {
        float ang = particleSupport_.rand01() * 6.2831853f;
        float r = particleSupport_.randRange(0.0f, 0.35f);
        float h = particleSupport_.randRange(0.05f, 0.55f);

        glm::vec3 offset(std::cos(ang) * r, h, std::sin(ang) * r);
        const glm::vec3 origin = startPos + offset;
        const glm::vec3 delta = endPos - origin;
        const glm::vec3 vel = delta * (2.0f / T);
        const glm::vec3 accel = delta * (-2.0f / (T * T));

        ParticleSystem::Particle p;
        p.pos = origin;
        p.vel = vel;
        p.accel = accel;
        p.maxLifeSec = T;
        p.lifeSec = T;
        p.sizePx = particleSupport_.randRange(cfg.minSize, cfg.maxSize);
        p.seed = particleSupport_.rand01();

        particleSupport_.particles().emit(p);
    }
}
