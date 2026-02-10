// src/game/vfx/ClawSwipeVFX.cpp
#include "ClawSwipeVFX.h"

#include <algorithm>

#include "engine/render/Camera3D.h"

void ClawSwipeVFX::ensureConfigured() {
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

float ClawSwipeVFX::rand01() {
    return engine::random::nextFloat01(rng);
}

float ClawSwipeVFX::randRange(float a, float b) {
    if (b < a) std::swap(a, b);
    return a + (b - a) * rand01();
}

glm::vec3 ClawSwipeVFX::safeForwardXZ(const glm::vec3& v) const {
    glm::vec3 f(v.x, 0.0f, v.z);
    const float len = glm::length(f);
    if (len <= 0.0001f) return glm::vec3(0.0f, 0.0f, 1.0f);
    return f / len;
}

void ClawSwipeVFX::update(float dt) {
    ensureConfigured();
    particles.update(dt);
}

void ClawSwipeVFX::render(const Camera3D& camera) {
    ensureConfigured();
    particles.render(camera);
}

void ClawSwipeVFX::emitAt(const glm::vec3& worldPos, const glm::vec3& attackForward, bool metallic) {
    ensureConfigured();

    const glm::vec3 fwd = safeForwardXZ(attackForward);
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

    const int count = engine::random::rangeInclusive(rng, minP, maxP);
    if (count <= 0) return;

    const float seedMin = metallic ? 0.60f : 0.08f;
    const float seedMax = metallic ? 0.96f : 0.48f;

    for (int i = 0; i < count; ++i) {
        const float side = randRange(-1.0f, 1.0f);
        const float jitterRadius = metallic ? cfg.spawnRadius : 0.0f;
        const glm::vec3 jitter =
            right * randRange(-jitterRadius, jitterRadius) +
            glm::vec3(0.0f, randRange(-jitterRadius, jitterRadius), 0.0f) +
            fwd * randRange(-jitterRadius * 0.35f, jitterRadius * 0.35f);

        glm::vec3 vel(0.0f);
        if (!metallic) {
            // Tiny intentional drift so the hit does not look fully static.
            vel = fwd * 0.04f
                + right * randRange(-0.008f, 0.008f)
                + glm::vec3(0.0f, 0.01f, 0.0f);
        } else {
            glm::vec3 dir = fwd + right * side * 0.65f + glm::vec3(0.0f, randRange(0.05f, 0.30f), 0.0f);
            const float len = glm::length(dir);
            if (len > 0.0001f) dir /= len;
            else dir = fwd;

            const float speed = randRange(speedMin, speedMax);
            vel = dir * speed + right * randRange(-lateralSpeed, lateralSpeed);
        }

        ParticleSystem::Particle p;
        p.pos = worldPos + glm::vec3(0.0f, cfg.impactYOffset, 0.0f) + jitter;
        p.vel = vel;
        p.maxLifeSec = randRange(lifeMin, lifeMax);
        p.lifeSec = p.maxLifeSec;
        p.sizePx = randRange(sizeMin, sizeMax);
        p.seed = randRange(seedMin, seedMax);
        if (!metallic) {
            // Cancel system-level acceleration for scratch so it does not float away.
            p.accel = -cfg.acceleration;
        }
        particles.emit(p);
    }
}
