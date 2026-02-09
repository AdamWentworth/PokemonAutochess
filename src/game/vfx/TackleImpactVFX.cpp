// src/game/vfx/TackleImpactVFX.cpp
#include "TackleImpactVFX.h"

#include <algorithm>
#include <cmath>

#include "engine/render/Camera3D.h"

static constexpr float kTwoPi = 6.28318530718f;

void TackleImpactVFX::ensureConfigured() {
    if (configured) return;

    // Burst (comic starburst)
    burstParticles.setShaderPaths(cfg.vertShaderPath, cfg.burstFragShaderPath);
    burstParticles.setUseFlipbook(false);

    ParticleSystem::RenderSettings rsBurst;
    rsBurst.blend = cfg.burstBlend;
    rsBurst.depthTest = cfg.burstDepthTest;
    rsBurst.depthWrite = cfg.burstDepthWrite;
    rsBurst.programPointSize = true;
    burstParticles.setRenderSettings(rsBurst);

    ParticleSystem::UpdateSettings usBurst;
    usBurst.acceleration = cfg.burstAcceleration;
    usBurst.dampingBase = cfg.burstDampingBase;
    burstParticles.setUpdateSettings(usBurst);

    burstParticles.setPointScale(cfg.burstPointScale);

    // Sparks (fast dots)
    sparkParticles.setShaderPaths(cfg.vertShaderPath, cfg.sparkFragShaderPath);
    sparkParticles.setUseFlipbook(false);

    ParticleSystem::RenderSettings rsSparks;
    rsSparks.blend = cfg.sparkBlend;
    rsSparks.depthTest = cfg.sparkDepthTest;
    rsSparks.depthWrite = cfg.sparkDepthWrite;
    rsSparks.programPointSize = true;
    sparkParticles.setRenderSettings(rsSparks);

    ParticleSystem::UpdateSettings usSparks;
    usSparks.acceleration = cfg.sparkAcceleration;
    usSparks.dampingBase = cfg.sparkDampingBase;
    sparkParticles.setUpdateSettings(usSparks);

    sparkParticles.setPointScale(cfg.sparkPointScale);

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
    burstParticles.update(dt);
    sparkParticles.update(dt);
}

void TackleImpactVFX::render(const Camera3D& camera) {
    ensureConfigured();
    burstParticles.render(camera);
    sparkParticles.render(camera);
}

void TackleImpactVFX::emitAt(const glm::vec3& worldPos) {
    ensureConfigured();

    // ----- Burst layer -----
    int minB = std::max(0, cfg.burstMinParticles);
    int maxB = std::max(minB, cfg.burstMaxParticles);
    int burstCount = engine::random::rangeInclusive(rng, minB, maxB);

    const float burstLifeMin = std::max(0.05f, cfg.burstMinLifeSec);
    const float burstLifeMax = std::max(burstLifeMin, cfg.burstMaxLifeSec);
    const float burstSizeMin = std::max(0.01f, cfg.burstMinSize);
    const float burstSizeMax = std::max(burstSizeMin, cfg.burstMaxSize);
    const float burstUpMin = std::clamp(cfg.burstMinUpward, -1.0f, 1.0f);
    const float burstUpMax = std::clamp(cfg.burstMaxUpward, -1.0f, 1.0f);

    for (int i = 0; i < burstCount; ++i) {
        float yaw = rand01() * kTwoPi;
        float up = randRange(burstUpMin, burstUpMax);
        up = std::clamp(up, -1.0f, 1.0f);
        float lateral = std::sqrt(std::max(0.0f, 1.0f - up * up));

        glm::vec3 dir(std::cos(yaw) * lateral, up, std::sin(yaw) * lateral);
        float speed = randRange(cfg.burstMinSpeed, cfg.burstMaxSpeed);
        glm::vec3 vel = dir * speed;

        glm::vec3 jitter(
            randRange(-cfg.burstSpawnRadius, cfg.burstSpawnRadius),
            randRange(-cfg.burstSpawnRadius, cfg.burstSpawnRadius),
            randRange(-cfg.burstSpawnRadius, cfg.burstSpawnRadius));

        ParticleSystem::Particle p;
        p.pos = worldPos + glm::vec3(0.0f, cfg.burstImpactYOffset, 0.0f) + jitter;
        p.vel = vel;
        p.maxLifeSec = randRange(burstLifeMin, burstLifeMax);
        p.lifeSec = p.maxLifeSec;
        p.sizePx = randRange(burstSizeMin, burstSizeMax);
        p.seed = randRange(0.10f, 0.45f); // primary

        burstParticles.emit(p);
    }

    if (cfg.enableSecondary) {
        float yaw = rand01() * kTwoPi;
        float up = randRange(burstUpMin, burstUpMax);
        up = std::clamp(up, -1.0f, 1.0f);
        float lateral = std::sqrt(std::max(0.0f, 1.0f - up * up));
        glm::vec3 dir(std::cos(yaw) * lateral, up, std::sin(yaw) * lateral);
        float speed = randRange(cfg.burstMinSpeed, cfg.burstMaxSpeed) * cfg.secondarySpeedScale;

        ParticleSystem::Particle p;
        p.pos = worldPos + glm::vec3(0.0f, cfg.burstImpactYOffset, 0.0f);
        p.vel = dir * speed;
        p.maxLifeSec = randRange(burstLifeMin, burstLifeMax) * cfg.secondaryLifeScale;
        p.lifeSec = p.maxLifeSec;
        p.sizePx = randRange(burstSizeMin, burstSizeMax) * cfg.secondarySizeScale;
        p.seed = randRange(0.60f, 0.95f); // secondary
        burstParticles.emit(p);
    }

    // ----- Sparks -----
    int minS = std::max(0, cfg.sparkMinParticles);
    int maxS = std::max(minS, cfg.sparkMaxParticles);
    int sparkCount = engine::random::rangeInclusive(rng, minS, maxS);
    if (sparkCount <= 0) return;

    const float sparkLifeMin = std::max(0.03f, cfg.sparkMinLifeSec);
    const float sparkLifeMax = std::max(sparkLifeMin, cfg.sparkMaxLifeSec);
    const float sparkSizeMin = std::max(0.01f, cfg.sparkMinSize);
    const float sparkSizeMax = std::max(sparkSizeMin, cfg.sparkMaxSize);
    const float sparkUpMin = std::clamp(cfg.sparkMinUpward, -1.0f, 1.0f);
    const float sparkUpMax = std::clamp(cfg.sparkMaxUpward, -1.0f, 1.0f);

    for (int i = 0; i < sparkCount; ++i) {
        float yaw = rand01() * kTwoPi;
        float up = randRange(sparkUpMin, sparkUpMax);
        up = std::clamp(up, -1.0f, 1.0f);
        float lateral = std::sqrt(std::max(0.0f, 1.0f - up * up));

        glm::vec3 dir(std::cos(yaw) * lateral, up, std::sin(yaw) * lateral);
        float speed = randRange(cfg.sparkMinSpeed, cfg.sparkMaxSpeed);
        glm::vec3 vel = dir * speed;

        glm::vec3 jitter(
            randRange(-cfg.sparkSpawnRadius, cfg.sparkSpawnRadius),
            randRange(-cfg.sparkSpawnRadius, cfg.sparkSpawnRadius),
            randRange(-cfg.sparkSpawnRadius, cfg.sparkSpawnRadius));

        ParticleSystem::Particle p;
        p.pos = worldPos + glm::vec3(0.0f, cfg.burstImpactYOffset, 0.0f) + jitter;
        p.vel = vel;
        p.maxLifeSec = randRange(sparkLifeMin, sparkLifeMax);
        p.lifeSec = p.maxLifeSec;
        p.sizePx = randRange(sparkSizeMin, sparkSizeMax);
        p.seed = rand01();

        sparkParticles.emit(p);
    }
}
