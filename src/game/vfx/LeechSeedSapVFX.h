// src/game/vfx/LeechSeedSapVFX.h
#pragma once

#include <cstdint>
#include <string>
#include <glm/glm.hpp>

#include "engine/core/Random.h"
#include "engine/vfx/ParticleSystem.h"

class Camera3D;

class LeechSeedSapVFX {
public:
    struct Config {
        int minParticles = 5;
        int maxParticles = 8;

        float capsuleRadius = 0.28f;
        float capsuleHeight = 0.60f;    // full height
        float centerYOffset = 0.30f;    // world-space offset from base to capsule center
        float surfaceJitter = 0.02f;

        float minLifeSec = 1.0f;
        float maxLifeSec = 1.0f;

        float minSize = 0.18f;
        float maxSize = 0.28f;

        float minSpeed = 0.006f;
        float maxSpeed = 0.025f;

        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string fragShaderPath = "assets/shaders/vfx/leech_root.frag";

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Alpha;
        bool depthTest  = true;
        bool depthWrite = false;

        glm::vec3 acceleration = glm::vec3(0.0f);
        float dampingBase = 0.80f;

        float pointScale = 520.0f;
    };

public:
    LeechSeedSapVFX() = default;

    void setConfig(const Config& c) {
        cfg = c;
        configured = false;
    }

    void update(float dt);
    void render(const Camera3D& camera);

    void emitAt(const glm::vec3& worldBasePos, float scaleFactor);

private:
    void ensureConfigured();
    float rand01();
    float randRange(float a, float b);

private:
    ParticleSystem particles;
    Config cfg{};
    bool configured = false;

    engine::XorShift32 rng{0xA17C3Du};
};
