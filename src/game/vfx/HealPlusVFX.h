// src/game/vfx/HealPlusVFX.h
#pragma once

#include <cstdint>
#include <string>
#include <glm/glm.hpp>

#include "game/vfx/shared/SimpleParticleVfxSupport.h"

class Camera3D;

class HealPlusVFX {
public:
    struct Config {
        int minParticles = 6;
        int maxParticles = 10;

        float radius = 0.35f;
        float minHeight = 0.2f;
        float maxHeight = 0.8f;

        float minLifeSec = 1.0f;
        float maxLifeSec = 1.0f;

        float minSize = 0.18f;
        float maxSize = 0.28f;

        float minSpeed = 0.12f;
        float maxSpeed = 0.32f;

        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string fragShaderPath = "assets/shaders/vfx/heal_plus.frag";

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Alpha;
        bool depthTest  = false;
        bool depthWrite = false;

        glm::vec3 acceleration = glm::vec3(0.0f);
        float dampingBase = 0.70f;

        float pointScale = 640.0f;
    };

public:
    HealPlusVFX() = default;

    void setConfig(const Config& c) {
        cfg = c;
        particleSupport_.markDirty();
    }

    void update(float dt);
    void render(const Camera3D& camera);
    const ParticleSystem& getParticles() const { return particleSupport_.particles(); }

    void emitAt(const glm::vec3& worldPos);

private:
    game::particle_vfx::shared::SimpleParticleVfxSupport particleSupport_{0xC0FFEEu};
    Config cfg{};
};
