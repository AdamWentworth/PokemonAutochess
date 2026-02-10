// src/game/vfx/GrowlWaveVFX.h
#pragma once

#include <string>
#include <glm/glm.hpp>

#include "engine/core/Random.h"
#include "engine/vfx/ParticleSystem.h"

class Camera3D;

class GrowlWaveVFX {
public:
    struct Config {
        int minParticles = 1;
        int maxParticles = 1;

        float spawnForwardOffset = 0.20f;
        float spawnHeightOffset = 0.40f;
        float lateralSpawnJitter = 0.00f;
        float forwardSpawnJitter = 0.00f;

        float minSpeed = 1.30f;
        float maxSpeed = 1.60f;
        float lateralSpeed = 0.00f;

        float minLifeSec = 0.34f;
        float maxLifeSec = 0.50f;
        float minSize = 0.48f;
        float maxSize = 0.70f;

        float minUpward = 0.00f;
        float maxUpward = 0.00f;

        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string fragShaderPath = "assets/shaders/vfx/growl_wave.frag";

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Additive;
        bool depthTest = true;
        bool depthWrite = false;

        glm::vec3 acceleration = glm::vec3(0.0f, -0.45f, 0.0f);
        float dampingBase = 0.25f;
        float pointScale = 520.0f;
    };

public:
    GrowlWaveVFX() = default;

    void setConfig(const Config& c) {
        cfg = c;
        configured = false;
    }

    void update(float dt);
    void render(const Camera3D& camera);

    void emitFrom(const glm::vec3& mouthWorldPos, const glm::vec3& forwardDir);

private:
    void ensureConfigured();
    float rand01();
    float randRange(float a, float b);
    glm::vec3 safeForwardXZ(const glm::vec3& v) const;

private:
    ParticleSystem particles;
    Config cfg{};
    bool configured = false;
    engine::XorShift32 rng{0xA17F2Du};
};
