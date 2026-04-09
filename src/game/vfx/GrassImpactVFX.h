// src/game/vfx/GrassImpactVFX.h
#pragma once

#include <cstdint>
#include <string>
#include <glm/glm.hpp>

#include "game/vfx/shared/SimpleParticleVfxSupport.h"

class Camera3D;

class GrassImpactVFX {
public:
    struct Config {
        int minParticles = 8;
        int maxParticles = 14;

        float spawnRadius = 0.12f;
        float impactYOffset = 0.35f;

        float minSpeed = 0.7f;
        float maxSpeed = 1.8f;

        float minLifeSec = 0.35f;
        float maxLifeSec = 0.80f;

        float minSize = 0.10f;
        float maxSize = 0.20f;

        float minUpward = 0.35f;
        float maxUpward = 0.95f;

        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string fragShaderPath = "assets/shaders/vfx/leaf_impact.frag";

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Alpha;
        bool depthTest  = true;
        bool depthWrite = false;

        glm::vec3 acceleration = glm::vec3(0.0f, -2.8f, 0.0f);
        float dampingBase = 0.35f;

        float pointScale = 420.0f;
    };

public:
    GrassImpactVFX() = default;

    void setConfig(const Config& c) {
        cfg = c;
        particleSupport_.markDirty();
    }

    void update(float dt);
    void render(const Camera3D& camera);
    const ParticleSystem& getParticles() const { return particleSupport_.particles(); }

    void emitAt(const glm::vec3& worldPos);

private:
    game::particle_vfx::shared::SimpleParticleVfxSupport particleSupport_{0x5B1A53u};
    Config cfg{};
};
