// src/game/vfx/AquaSwooshVFX.h
#pragma once

#include <string>
#include <glm/glm.hpp>

#include "engine/core/Random.h"
#include "engine/vfx/ParticleSystem.h"

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
        configured = false;
    }

    void update(float dt);
    void render(const Camera3D& camera);

    void emitAt(const glm::vec3& worldPos, const glm::vec3& attackForward, Style style);

private:
    void ensureConfigured();
    float rand01();
    float randRange(float a, float b);
    glm::vec3 safeForwardXZ(const glm::vec3& v) const;

private:
    ParticleSystem particles;
    Config cfg{};
    bool configured = false;
    engine::XorShift32 rng{0x74CE11u};
};

