// src/game/vfx/LeechSeedDrainVFX.cpp
#include "LeechSeedDrainVFX.h"

#include <algorithm>
#include <cmath>

#include "engine/render/Camera3D.h"

void LeechSeedDrainVFX::ensureConfigured() {
    if (configured) return;

    particles.setShaderPaths(cfg.vertShaderPath, cfg.fragShaderPath);
    particles.setUseFlipbook(false);

    ParticleSystem::RenderSettings rs;
    rs.blend = cfg.blend;
    rs.depthTest = cfg.depthTest;
    rs.depthWrite = cfg.depthWrite;
    rs.programPointSize = true;
    particles.setRenderSettings(rs);

    ParticleSystem::UpdateSettings us;
    us.acceleration = cfg.acceleration;
    us.dampingBase = cfg.dampingBase;
    particles.setUpdateSettings(us);

    particles.setPointScale(cfg.pointScale);

    configured = true;
}

float LeechSeedDrainVFX::rand01() {
    return engine::random::nextFloat01(rng);
}

float LeechSeedDrainVFX::randRange(float a, float b) {
    if (b < a) std::swap(a, b);
    return a + (b - a) * rand01();
}

void LeechSeedDrainVFX::update(float dt) {
    ensureConfigured();
    particles.update(dt);
}

void LeechSeedDrainVFX::render(const Camera3D& camera) {
    ensureConfigured();
    particles.render(camera);
}

void LeechSeedDrainVFX::emitBetween(const glm::vec3& startPos,
                                    const glm::vec3& endPos,
                                    float travelSec) {
    ensureConfigured();

    const float T = std::max(0.10f, travelSec);

    int minP = std::max(0, cfg.minParticles);
    int maxP = std::max(minP, cfg.maxParticles);
    int count = engine::random::rangeInclusive(rng, minP, maxP);
    if (count <= 0) return;

    for (int i = 0; i < count; ++i) {
        float ang = rand01() * 6.2831853f;
        float r = randRange(0.0f, 0.35f);
        float h = randRange(0.05f, 0.55f);

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
        p.sizePx = randRange(cfg.minSize, cfg.maxSize);
        p.seed = rand01();

        particles.emit(p);
    }
}
