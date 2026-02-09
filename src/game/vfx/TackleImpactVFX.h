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
        int minParticles = 4;
        int maxParticles = 7;

        float spawnRadius = 0.08f;
        float impactYOffset = 0.30f;

        float minSpeed = 0.4f;
        float maxSpeed = 1.1f;

        float minLifeSec = 0.20f;
        float maxLifeSec = 0.45f;

        float minSize = 0.10f;
        float maxSize = 0.22f;

        float minUpward = 0.20f;
        float maxUpward = 0.70f;

        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string fragShaderPath = "assets/shaders/vfx/splat_impact.frag";

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Alpha;
        bool depthTest  = true;
        bool depthWrite = false;

        glm::vec3 acceleration = glm::vec3(0.0f, -2.0f, 0.0f);
        float dampingBase = 0.40f;

        float pointScale = 420.0f;
    };

public:
    TackleImpactVFX() = default;

    void setConfig(const Config& c) {
        cfg = c;
        configured = false;
    }

    void update(float dt);
    void render(const Camera3D& camera);

    void emitAt(const glm::vec3& worldPos);

private:
    void ensureConfigured();
    float rand01();
    float randRange(float a, float b);

private:
    ParticleSystem particles;
    Config cfg{};
    bool configured = false;

    engine::XorShift32 rng{0x4C11AFu};
};
