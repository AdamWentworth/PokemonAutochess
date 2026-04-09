// src/game/vfx/AquaSwooshVFX.h
#pragma once

#include <string>
#include <glm/glm.hpp>

#include "game/vfx/shared/SimpleParticleVfxSupport.h"

class Camera3D;

class AquaSwooshVFX {
public:
    enum class Style {
        TailWhip,
        Bubble,
        WaterGun
    };

    struct Config {
        float impactYOffset = 0.28f;
        float spawnRadius = 0.10f;

        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string fragShaderPath = "assets/shaders/vfx/aqua_swoosh.frag";

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Additive;
        bool depthTest = true;
        bool depthWrite = false;

        glm::vec3 acceleration = glm::vec3(0.0f, -1.65f, 0.0f);
        float dampingBase = 0.22f;
        float pointScale = 480.0f;
    };

public:
    AquaSwooshVFX() = default;

    void setConfig(const Config& c) {
        cfg = c;
        particleSupport_.markDirty();
    }

    void update(float dt);
    void render(const Camera3D& camera);
    const ParticleSystem& getParticles() const { return particleSupport_.particles(); }

    void emitAt(const glm::vec3& worldPos, const glm::vec3& attackForward, Style style);

private:
    game::particle_vfx::shared::SimpleParticleVfxSupport particleSupport_{0x74CE11u};
    Config cfg{};
};
