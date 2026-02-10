// src/game/vfx/GrowlWaveVFX.cpp
#include "GrowlWaveVFX.h"

#include <algorithm>
#include <cmath>

#include "engine/render/Camera3D.h"

void GrowlWaveVFX::ensureConfigured() {
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

float GrowlWaveVFX::rand01() {
    return engine::random::nextFloat01(rng);
}

float GrowlWaveVFX::randRange(float a, float b) {
    if (b < a) std::swap(a, b);
    return a + (b - a) * rand01();
}

glm::vec3 GrowlWaveVFX::safeForwardXZ(const glm::vec3& v) const {
    glm::vec3 f(v.x, 0.0f, v.z);
    const float len = glm::length(f);
    if (len <= 0.0001f) return glm::vec3(0.0f, 0.0f, 1.0f);
    return f / len;
}

void GrowlWaveVFX::update(float dt) {
    ensureConfigured();
    particles.update(dt);
}

void GrowlWaveVFX::render(const Camera3D& camera) {
    ensureConfigured();
    particles.render(camera);
}

void GrowlWaveVFX::emitFrom(const glm::vec3& mouthWorldPos, const glm::vec3& forwardDir) {
    ensureConfigured();

    const glm::vec3 fwd = safeForwardXZ(forwardDir);
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), fwd);
    if (glm::length(right) <= 0.0001f) right = glm::vec3(1.0f, 0.0f, 0.0f);
    else right = glm::normalize(right);

    // Growl should always read as one single cone emission.
    const int count = 1;

    const float lifeMin = std::max(0.05f, cfg.minLifeSec);
    const float lifeMax = std::max(lifeMin, cfg.maxLifeSec);
    const float sizeMin = std::max(0.02f, cfg.minSize);
    const float sizeMax = std::max(sizeMin, cfg.maxSize);
    const float upMin = std::clamp(cfg.minUpward, -1.0f, 1.0f);
    const float upMax = std::clamp(cfg.maxUpward, -1.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        const float spread = 0.0f;
        const float up = randRange(upMin, upMax);

        glm::vec3 dir = fwd + right * (spread * 0.35f);
        dir.y = up;
        const float len = glm::length(dir);
        if (len > 0.0001f) dir /= len;
        else dir = fwd;

        const float speed = randRange(cfg.minSpeed, cfg.maxSpeed);
        glm::vec3 vel = dir * speed;

        const glm::vec3 spawn =
            mouthWorldPos +
            glm::vec3(0.0f, cfg.spawnHeightOffset, 0.0f) +
            fwd * (cfg.spawnForwardOffset + randRange(-cfg.forwardSpawnJitter, cfg.forwardSpawnJitter)) +
            right * randRange(-cfg.lateralSpawnJitter, cfg.lateralSpawnJitter);

        ParticleSystem::Particle p;
        p.pos = spawn;
        p.vel = vel;
        p.maxLifeSec = randRange(lifeMin, lifeMax);
        p.lifeSec = p.maxLifeSec;
        p.sizePx = randRange(sizeMin, sizeMax);
        p.seed = rand01();
        particles.emit(p);
    }
}
