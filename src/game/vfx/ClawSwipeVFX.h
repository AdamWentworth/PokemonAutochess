// src/game/vfx/ClawSwipeVFX.h
#pragma once

#include <string>
#include <glm/glm.hpp>

#include "game/vfx/shared/SimpleParticleVfxSupport.h"

class Camera3D;

class ClawSwipeVFX {
public:
    struct Config {
        int minParticles = 4;
        int maxParticles = 7;

        float spawnRadius = 0.10f;
        float impactYOffset = 0.30f;

        float minSpeed = 1.30f;
        float maxSpeed = 2.60f;
        float lateralSpeed = 1.15f;

        float minLifeSec = 0.10f;
        float maxLifeSec = 0.22f;

        float minSize = 0.16f;
        float maxSize = 0.32f;

        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string fragShaderPath = "assets/shaders/vfx/claw_swipe.frag";

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Alpha;
        bool depthTest = true;
        bool depthWrite = false;

        glm::vec3 acceleration = glm::vec3(0.0f, -2.20f, 0.0f);
        float dampingBase = 0.20f;
        float pointScale = 520.0f;
    };

public:
    ClawSwipeVFX() = default;

    void setConfig(const Config& c) {
        cfg = c;
        particleSupport_.markDirty();
    }

    void update(float dt);
    void render(const Camera3D& camera);
    const ParticleSystem& getParticles() const { return particleSupport_.particles(); }

    // metallic=false: white scratch swipe
    // metallic=true:  silver sparkling metal claw swipe
    void emitAt(const glm::vec3& worldPos, const glm::vec3& attackForward, bool metallic);

private:
    game::particle_vfx::shared::SimpleParticleVfxSupport particleSupport_{0x9AB31Fu};
    Config cfg{};
};
