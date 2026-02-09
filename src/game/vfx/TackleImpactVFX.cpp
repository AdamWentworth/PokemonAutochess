// src/game/vfx/TackleImpactVFX.cpp
#include "TackleImpactVFX.h"

#include <algorithm>
#include <cmath>

#include "engine/render/Camera3D.h"

static constexpr float kTwoPi = 6.28318530718f;

void TackleImpactVFX::ensureConfigured() {
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

float TackleImpactVFX::rand01() {
    return engine::random::nextFloat01(rng);
}

float TackleImpactVFX::randRange(float a, float b) {
    if (b < a) std::swap(a, b);
    return a + (b - a) * rand01();
}

void TackleImpactVFX::update(float dt) {
    ensureConfigured();
    particles.update(dt);
}

void TackleImpactVFX::render(const Camera3D& camera) {
    ensureConfigured();
    particles.render(camera);
}

void TackleImpactVFX::emitAt(const glm::vec3& worldPos) {
    ensureConfigured();

    int minP = std::max(0, cfg.minParticles);
    int maxP = std::max(minP, cfg.maxParticles);
    int count = engine::random::rangeInclusive(rng, minP, maxP);
    if (count <= 0) return;

    const float lifeMin = std::max(0.05f, cfg.minLifeSec);
    const float lifeMax = std::max(lifeMin, cfg.maxLifeSec);
    const float sizeMin = std::max(0.01f, cfg.minSize);
    const float sizeMax = std::max(sizeMin, cfg.maxSize);
    const float minUp = std::clamp(cfg.minUpward, -1.0f, 1.0f);
    const float maxUp = std::clamp(cfg.maxUpward, -1.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        float yaw = rand01() * kTwoPi;
        float up = randRange(minUp, maxUp);
        up = std::clamp(up, -1.0f, 1.0f);
        float lateral = std::sqrt(std::max(0.0f, 1.0f - up * up));

        glm::vec3 dir(std::cos(yaw) * lateral, up, std::sin(yaw) * lateral);
        float speed = randRange(cfg.minSpeed, cfg.maxSpeed);
        glm::vec3 vel = dir * speed;

        glm::vec3 jitter(
            randRange(-cfg.spawnRadius, cfg.spawnRadius),
            randRange(-cfg.spawnRadius, cfg.spawnRadius),
            randRange(-cfg.spawnRadius, cfg.spawnRadius));

        ParticleSystem::Particle p;
        p.pos = worldPos + glm::vec3(0.0f, cfg.impactYOffset, 0.0f) + jitter;
        p.vel = vel;
        p.maxLifeSec = randRange(lifeMin, lifeMax);
        p.lifeSec = p.maxLifeSec;
        p.sizePx = randRange(sizeMin, sizeMax);
        p.seed = rand01();

        particles.emit(p);
    }
}
