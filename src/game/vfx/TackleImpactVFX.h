// src/game/vfx/TackleImpactVFX.h
#pragma once

#include <cstdint>
#include <string>
#include <glm/glm.hpp>

#include "engine/core/Random.h"
#include "engine/vfx/ParticleSystem.h"

class Camera3D;

class TackleImpactVFX {
public:
    struct Config {
        // Burst (comic "smack" shape)
        int burstMinParticles = 1;
        int burstMaxParticles = 1;

        float burstSpawnRadius = 0.06f;
        float burstImpactYOffset = 0.26f;
        float impactEdgeOffset = 1.0f;

        float burstMinSpeed = 0.05f;
        float burstMaxSpeed = 0.35f;

        float burstMinLifeSec = 0.10f;
        float burstMaxLifeSec = 0.22f;

        float burstMinSize = 0.55f;
        float burstMaxSize = 0.95f;

        float burstMinUpward = 0.05f;
        float burstMaxUpward = 0.65f;

        bool  enableSecondary = true;
        float secondarySizeScale = 0.80f;
        float secondaryLifeScale = 0.90f;
        float secondarySpeedScale = 0.85f;

        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string burstFragShaderPath = "assets/shaders/vfx/splat_impact.frag";
        std::string sparkFragShaderPath = "assets/shaders/vfx/impact_spark.frag";

        ParticleSystem::BlendMode burstBlend = ParticleSystem::BlendMode::Additive;
        bool burstDepthTest  = true;
        bool burstDepthWrite = false;

        glm::vec3 burstAcceleration = glm::vec3(0.0f, -0.8f, 0.0f);
        float burstDampingBase = 0.55f;

        float burstPointScale = 620.0f;

        // Sparks (small, fast dots)
        int   sparkMinParticles = 8;
        int   sparkMaxParticles = 14;
        float sparkSpawnRadius = 0.03f;
        float sparkMinSpeed = 1.4f;
        float sparkMaxSpeed = 3.4f;
        float sparkMinLifeSec = 0.07f;
        float sparkMaxLifeSec = 0.14f;
        float sparkMinSize = 0.040f;
        float sparkMaxSize = 0.090f;
        float sparkMinUpward = 0.05f;
        float sparkMaxUpward = 0.90f;
        glm::vec3 sparkAcceleration = glm::vec3(0.0f, -7.2f, 0.0f);
        float sparkDampingBase = 0.22f;
        float sparkPointScale = 460.0f;
        ParticleSystem::BlendMode sparkBlend = ParticleSystem::BlendMode::Additive;
        bool sparkDepthTest  = true;
        bool sparkDepthWrite = false;
    };

public:
    TackleImpactVFX() = default;

    void setConfig(const Config& c) {
        cfg = c;
        configured = false;
    }

    const Config& getConfig() const { return cfg; }

    void update(float dt);
    void render(const Camera3D& camera);
    const ParticleSystem& getBurstParticles() const { return burstParticles; }
    const ParticleSystem& getSparkParticles() const { return sparkParticles; }

    void emitAt(const glm::vec3& worldPos);

private:
    void ensureConfigured();
    float rand01();
    float randRange(float a, float b);

private:
    ParticleSystem burstParticles;
    ParticleSystem sparkParticles;
    Config cfg{};
    bool configured = false;

    engine::XorShift32 rng{0x4C11AFu};
};
