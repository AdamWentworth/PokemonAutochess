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
        // Shared spawn anchor near mouth.
        float spawnForwardOffset = 0.20f;
        float spawnHeightOffset = 0.40f;

        // Cone layer (single cone only).
        float coneMinSpeed = 0.78f;
        float coneMaxSpeed = 0.98f;
        float coneMinLifeSec = 0.46f;
        float coneMaxLifeSec = 0.68f;
        float coneMinSize = 0.60f;
        float coneMaxSize = 0.88f;
        float conePointScale = 900.0f;
        float coneMaxLateralDrift = 0.12f;
        float coneMaxVerticalDrift = 0.04f;

        // Ring layer (lead ring at mouth + trailing rings down the cone axis).
        bool emitRing = true;
        float ringForwardOffset = 0.03f;
        float ringMinSpeed = 0.65f;
        float ringMaxSpeed = 0.95f;
        float ringMinLifeSec = 0.50f;
        float ringMaxLifeSec = 0.82f;
        float ringMinSize = 1.05f;
        float ringMaxSize = 1.45f;
        float ringPointScale = 1060.0f;
        int ringTrailCount = 4;
        float ringTrailSpacingMin = 0.10f;
        float ringTrailSpacingMax = 0.18f;
        float ringTrailLateralJitter = 0.03f;
        float ringLeadSizeMul = 1.28f;
        float ringTrailSizeFalloff = 0.88f;
        float ringTrailLifeFalloff = 0.92f;
        float ringTrailSpeedFalloff = 0.90f;

        // Star glint layer.
        bool emitStars = false;
        int starMinParticles = 4;
        int starMaxParticles = 7;
        float starSpawnRadius = 0.22f;
        float starMinSpeed = 0.10f;
        float starMaxSpeed = 0.32f;
        float starMinLifeSec = 0.18f;
        float starMaxLifeSec = 0.32f;
        float starMinSize = 0.10f;
        float starMaxSize = 0.17f;
        float starPointScale = 560.0f;

        // Shaders + textures
        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string fragShaderPath = "assets/shaders/vfx/sprite_additive.frag";
        std::string coneTexturePath = "assets/vfx/textures/moves/growl/growl_cone_line.png";
        std::string ringTexturePath = "assets/vfx/textures/moves/growl/growl_ring_soft.png";
        std::string starTexturePath = "assets/vfx/textures/common/star_glint_01.png";

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Premultiplied;
        bool depthTest = true;
        bool depthWrite = false;

        glm::vec3 acceleration = glm::vec3(0.0f);
        float dampingBase = 0.55f;
        glm::vec3 starAcceleration = glm::vec3(0.0f, -0.25f, 0.0f);
        float starDampingBase = 0.35f;
    };

public:
    GrowlWaveVFX() = default;

    void setConfig(const Config& c) {
        cfg = c;
        configured = false;
    }

    void update(float dt);
    void render(const Camera3D& camera);

    void emitFrom(const glm::vec3& mouthWorldPos,
                  const glm::vec3& forwardDir,
                  const glm::mat4* viewMatrix = nullptr);

private:
    void configureLayer(ParticleSystem& ps,
                        const std::string& texturePath,
                        float pointScale,
                        const glm::vec3& acceleration,
                        float dampingBase);
    void ensureConfigured();
    float rand01();
    float randRange(float a, float b);
    glm::vec3 safeForwardXZ(const glm::vec3& v) const;

private:
    ParticleSystem coneParticles;
    ParticleSystem ringParticles;
    ParticleSystem starParticles;
    Config cfg{};
    bool configured = false;
    engine::XorShift32 rng{0xA17F2Du};
};
