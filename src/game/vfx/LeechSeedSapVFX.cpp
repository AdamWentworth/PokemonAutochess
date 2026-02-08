// src/game/vfx/LeechSeedSapVFX.cpp
#include "LeechSeedSapVFX.h"

#include <algorithm>
#include <cmath>

#include "engine/render/Camera3D.h"

static constexpr float kTwoPi = 6.28318530718f;

void LeechSeedSapVFX::ensureConfigured() {
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

float LeechSeedSapVFX::rand01() {
    return engine::random::nextFloat01(rng);
}

float LeechSeedSapVFX::randRange(float a, float b) {
    if (b < a) std::swap(a, b);
    return a + (b - a) * rand01();
}

void LeechSeedSapVFX::update(float dt) {
    ensureConfigured();
    particles.update(dt);
}

void LeechSeedSapVFX::render(const Camera3D& camera) {
    ensureConfigured();
    particles.render(camera);
}

void LeechSeedSapVFX::emitAt(const glm::vec3& worldBasePos, float scaleFactor) {
    ensureConfigured();

    int minP = std::max(0, cfg.minParticles);
    int maxP = std::max(minP, cfg.maxParticles);
    int count = engine::random::rangeInclusive(rng, minP, maxP);
    if (count <= 0) return;

    const float scale = (scaleFactor > 0.0f) ? scaleFactor : 1.0f;
    const float radius = std::max(0.02f, cfg.capsuleRadius * scale);
    const float height = std::max(radius * 2.0f, cfg.capsuleHeight * scale);
    const float halfHeight = std::max(0.0f, 0.5f * height - radius);
    const glm::vec3 center = worldBasePos + glm::vec3(0.0f, cfg.centerYOffset * scale, 0.0f);

    const float cylArea = (halfHeight > 0.0f) ? (2.0f * 3.14159265f * radius * (2.0f * halfHeight)) : 0.0f;
    const float sphArea = 4.0f * 3.14159265f * radius * radius;
    const float areaSum = cylArea + sphArea;

    for (int i = 0; i < count; ++i) {
        float ang = rand01() * kTwoPi;

        // Sample a point on the capsule surface.
        glm::vec3 pos = center;
        if (areaSum > 0.0f && rand01() * areaSum < cylArea) {
            // Cylinder side
            float y = randRange(-halfHeight, halfHeight);
            pos += glm::vec3(std::cos(ang) * radius, y, std::sin(ang) * radius);
        } else {
            // Hemisphere
            float sign = (rand01() < 0.5f) ? -1.0f : 1.0f;
            float cosT = randRange(0.0f, 1.0f);
            float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
            float phi = rand01() * kTwoPi;
            glm::vec3 hemi(std::cos(phi) * sinT, sign * cosT, std::sin(phi) * sinT);
            pos += glm::vec3(0.0f, sign * halfHeight, 0.0f) + hemi * radius;
        }

        // Small jitter along normal to avoid a perfect ring look.
        if (cfg.surfaceJitter > 0.0f) {
            glm::vec3 n = glm::normalize(pos - center);
            if (glm::dot(n, n) > 0.0f) {
                pos += n * randRange(-cfg.surfaceJitter, cfg.surfaceJitter);
            }
        }

        // Very subtle tangential drift to keep it hugging the body.
        float spd = randRange(cfg.minSpeed, cfg.maxSpeed);
        glm::vec3 tangent(-std::sin(ang), 0.0f, std::cos(ang));
        glm::vec3 vel = tangent * (spd * 0.12f);
        vel.y += spd * 0.03f;

        ParticleSystem::Particle p;
        p.pos = pos;
        p.vel = vel;
        p.maxLifeSec = randRange(cfg.minLifeSec, cfg.maxLifeSec);
        p.lifeSec = p.maxLifeSec;
        p.sizePx = randRange(cfg.minSize, cfg.maxSize);
        p.seed = rand01();

        particles.emit(p);
    }
}
