// src/game/vfx/HealPlusVFX.cpp
#include "HealPlusVFX.h"

#include <algorithm>
#include <cmath>

#include "engine/render/Camera3D.h"

static constexpr float kTwoPi = 6.28318530718f;

void HealPlusVFX::ensureConfigured() {
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

float HealPlusVFX::rand01() {
    return engine::random::nextFloat01(rng);
}

float HealPlusVFX::randRange(float a, float b) {
    if (b < a) std::swap(a, b);
    return a + (b - a) * rand01();
}

void HealPlusVFX::update(float dt) {
    ensureConfigured();
    particles.update(dt);
}

void HealPlusVFX::render(const Camera3D& camera) {
    ensureConfigured();
    particles.render(camera);
}

void HealPlusVFX::emitAt(const glm::vec3& worldPos) {
    ensureConfigured();

    int minP = std::max(0, cfg.minParticles);
    int maxP = std::max(minP, cfg.maxParticles);
    int count = engine::random::rangeInclusive(rng, minP, maxP);
    if (count <= 0) return;

    for (int i = 0; i < count; ++i) {
        float ang = rand01() * kTwoPi;
        float r = randRange(0.0f, cfg.radius);
        float h = randRange(cfg.minHeight, cfg.maxHeight);

        glm::vec3 offset(std::cos(ang) * r, h, std::sin(ang) * r);

        float spd = randRange(cfg.minSpeed, cfg.maxSpeed);
        glm::vec3 vel(
            std::cos(ang) * spd * 0.18f,
            spd * 0.25f,
            std::sin(ang) * spd * 0.18f);

        ParticleSystem::Particle p;
        p.pos = worldPos + offset;
        p.vel = vel;
        p.accel = glm::vec3(0.0f, 0.55f, 0.0f);
        p.maxLifeSec = randRange(cfg.minLifeSec, cfg.maxLifeSec);
        p.lifeSec = p.maxLifeSec;
        p.sizePx = randRange(cfg.minSize, cfg.maxSize);
        p.seed = rand01();

        particles.emit(p);
    }
}
