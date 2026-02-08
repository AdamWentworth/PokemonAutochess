// src/game/vfx/LeechSeedDrainVFX.h
#pragma once

#include <cstdint>
#include <string>
#include <glm/glm.hpp>

#include "engine/core/Random.h"
#include "engine/vfx/ParticleSystem.h"

class Camera3D;

class LeechSeedDrainVFX {
public:
    struct Config {
        int minParticles = 6;
        int maxParticles = 10;

        float minSize = 0.07f;
        float maxSize = 0.12f;

        float minLifeSec = 0.25f;
        float maxLifeSec = 0.45f;

        float pointScale = 520.0f;

        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string fragShaderPath = "assets/shaders/vfx/leech_drain_dot.frag";

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Alpha;
        bool depthTest  = false;
        bool depthWrite = false;

        glm::vec3 acceleration = glm::vec3(0.0f);
        float dampingBase = 1.0f;
    };

public:
    LeechSeedDrainVFX() = default;

    void setConfig(const Config& c) {
        cfg = c;
        configured = false;
    }

    void update(float dt);
    void render(const Camera3D& camera);

    void emitBetween(const glm::vec3& startPos,
                     const glm::vec3& endPos,
                     float travelSec);

private:
    void ensureConfigured();
    float rand01();
    float randRange(float a, float b);

private:
    ParticleSystem particles;
    Config cfg{};
    bool configured = false;

    engine::XorShift32 rng{0x15EEDu};
};
