// src/game/vfx/GrowlWaveVFX.cpp
#include "GrowlWaveVFX.h"

#include <algorithm>
#include <cmath>

#include "engine/render/Camera3D.h"

void GrowlWaveVFX::configureLayer(ParticleSystem& ps,
                                  const std::string& texturePath,
                                  float pointScale,
                                  const glm::vec3& acceleration,
                                  float dampingBase) {
    ps.setShaderPaths(cfg.vertShaderPath, cfg.fragShaderPath);
    ps.setUseFlipbook(true);
    ps.setFlipbook(texturePath, 1, 1, 1, 0.0f);

    ParticleSystem::RenderSettings rs;
    rs.blend = cfg.blend;
    rs.depthTest = cfg.depthTest;
    rs.depthWrite = cfg.depthWrite;
    rs.programPointSize = true;
    ps.setRenderSettings(rs);

    ParticleSystem::UpdateSettings us;
    us.acceleration = acceleration;
    us.dampingBase = dampingBase;
    ps.setUpdateSettings(us);

    ps.setPointScale(pointScale);
}

void GrowlWaveVFX::ensureConfigured() {
    if (configured) return;

    configureLayer(coneParticles,
                   cfg.coneTexturePath,
                   cfg.conePointScale,
                   cfg.acceleration,
                   cfg.dampingBase);

    configureLayer(ringParticles,
                   cfg.ringTexturePath,
                   cfg.ringPointScale,
                   cfg.acceleration,
                   cfg.dampingBase);

    configureLayer(starParticles,
                   cfg.starTexturePath,
                   cfg.starPointScale,
                   cfg.starAcceleration,
                   cfg.starDampingBase);

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
    coneParticles.update(dt);
    ringParticles.update(dt);
    starParticles.update(dt);
}

void GrowlWaveVFX::render(const Camera3D& camera) {
    ensureConfigured();
    coneParticles.render(camera);
    ringParticles.render(camera);
    starParticles.render(camera);
}

void GrowlWaveVFX::emitFrom(const glm::vec3& mouthWorldPos,
                            const glm::vec3& forwardDir,
                            const glm::mat4* viewMatrix) {
    ensureConfigured();

    const glm::vec3 fwd = safeForwardXZ(forwardDir);
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), fwd);
    if (glm::length(right) <= 0.0001f) right = glm::vec3(1.0f, 0.0f, 0.0f);
    else right = glm::normalize(right);

    const glm::vec3 origin =
        mouthWorldPos +
        glm::vec3(0.0f, cfg.spawnHeightOffset, 0.0f) +
        fwd * cfg.spawnForwardOffset;

    auto encodeScreenAngleSeed = [&]() -> float {
        // Base growl texture points right in screen space.
        // Compute screen-space direction from world forward via current view matrix.
        float angle = 0.0f;
        if (viewMatrix) {
            const glm::vec4 v4 = (*viewMatrix) * glm::vec4(fwd, 0.0f);
            const glm::vec2 d(v4.x, v4.y);
            const float len = glm::length(d);
            if (len > 0.0001f) {
                angle = std::atan2(d.y, d.x);
            }
        }
        return (angle + 3.14159265f) / 6.28318530f;
    };

    // Single cone particle (exactly one cone visual).
    {
        const float seedAngle = encodeScreenAngleSeed();
        ParticleSystem::Particle p;
        p.pos = origin;
        p.vel = fwd * randRange(cfg.coneMinSpeed, cfg.coneMaxSpeed) +
                right * randRange(-cfg.coneMaxLateralDrift, cfg.coneMaxLateralDrift) +
                glm::vec3(0.0f, randRange(-cfg.coneMaxVerticalDrift, cfg.coneMaxVerticalDrift), 0.0f);
        p.maxLifeSec = randRange(cfg.coneMinLifeSec, cfg.coneMaxLifeSec);
        p.lifeSec = p.maxLifeSec;
        p.sizePx = randRange(cfg.coneMinSize, cfg.coneMaxSize);
        p.seed = seedAngle;
        coneParticles.emit(p);
    }

    if (cfg.emitRing) {
        const float seedAngle = encodeScreenAngleSeed();
        const int trailCount = std::max(0, cfg.ringTrailCount);
        const int totalRings = 1 + trailCount; // lead ring + trails

        float forwardOffset = cfg.ringForwardOffset;
        const float spacingMin = std::max(0.0f, std::min(cfg.ringTrailSpacingMin, cfg.ringTrailSpacingMax));
        const float spacingMax = std::max(0.0f, std::max(cfg.ringTrailSpacingMin, cfg.ringTrailSpacingMax));

        const float speedFalloff = std::clamp(cfg.ringTrailSpeedFalloff, 0.35f, 1.0f);
        const float lifeFalloff = std::clamp(cfg.ringTrailLifeFalloff, 0.35f, 1.0f);
        const float sizeFalloff = std::clamp(cfg.ringTrailSizeFalloff, 0.35f, 1.0f);

        for (int i = 0; i < totalRings; ++i) {
            if (i > 0) {
                forwardOffset += randRange(spacingMin, spacingMax);
            }

            const float speedScale = std::pow(speedFalloff, static_cast<float>(i));
            const float lifeScale = std::pow(lifeFalloff, static_cast<float>(i));
            float sizeScale = std::pow(sizeFalloff, static_cast<float>(i));
            if (i == 0) sizeScale *= std::max(1.0f, cfg.ringLeadSizeMul);

            const float lateral = (i == 0)
                ? 0.0f
                : randRange(-cfg.ringTrailLateralJitter, cfg.ringTrailLateralJitter);
            const float vertical = (i == 0)
                ? 0.0f
                : randRange(-cfg.ringTrailLateralJitter * 0.35f, cfg.ringTrailLateralJitter * 0.35f);

            ParticleSystem::Particle p;
            p.pos = origin + fwd * forwardOffset + right * lateral + glm::vec3(0.0f, vertical, 0.0f);
            p.vel = fwd * randRange(cfg.ringMinSpeed, cfg.ringMaxSpeed) * speedScale;
            p.maxLifeSec = randRange(cfg.ringMinLifeSec, cfg.ringMaxLifeSec) * lifeScale;
            p.lifeSec = p.maxLifeSec;
            p.sizePx = randRange(cfg.ringMinSize, cfg.ringMaxSize) * sizeScale;
            p.seed = seedAngle;
            ringParticles.emit(p);
        }
    }

    if (cfg.emitStars) {
        const int starCount = engine::random::rangeInclusive(
            rng,
            std::max(0, cfg.starMinParticles),
            std::max(std::max(0, cfg.starMinParticles), cfg.starMaxParticles));

        for (int i = 0; i < starCount; ++i) {
            const glm::vec3 jitter =
                right * randRange(-cfg.starSpawnRadius, cfg.starSpawnRadius) +
                glm::vec3(0.0f, randRange(-cfg.starSpawnRadius * 0.45f, cfg.starSpawnRadius * 0.45f), 0.0f) +
                fwd * randRange(0.02f, cfg.starSpawnRadius * 1.35f);

            ParticleSystem::Particle p;
            p.pos = origin + jitter;
            p.vel = fwd * randRange(cfg.starMinSpeed, cfg.starMaxSpeed) +
                    right * randRange(-0.08f, 0.08f) +
                    glm::vec3(0.0f, randRange(-0.03f, 0.06f), 0.0f);
            p.maxLifeSec = randRange(cfg.starMinLifeSec, cfg.starMaxLifeSec);
            p.lifeSec = p.maxLifeSec;
            p.sizePx = randRange(cfg.starMinSize, cfg.starMaxSize);
            p.seed = rand01();
            starParticles.emit(p);
        }
    }
}
