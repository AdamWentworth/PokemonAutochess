// src/game/vfx/LeechSeedProjectileVFX.h
#pragma once

#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

#include "engine/core/Random.h"
#include "engine/vfx/ParticleSystem.h"
#include "game/PokemonInstance.h"

class Camera3D;
class Model;

class LeechSeedProjectileVFX {
public:
    struct Config {
        // Node to spawn from (bulb/back). Name preferred; index as fallback.
        std::string originNodeName = "spine_02";
        int originNodeIndex = 24;

        // Offset in the node's local space.
        glm::vec3 localOffset = glm::vec3(0.00f, 0.24f, -0.18f);

        // Additional world-space Y offset applied after node transform.
        float originWorldYOffset = 0.0f;
        float worldUpOffset = 0.25f;
        float worldBackOffset = 0.22f;

        // Target hit position offset (world-space Y).
        float impactYOffset = 0.35f;

        // Arc height above the higher of start/end.
        float arcHeight = 0.65f;

        // Visual size (interpreted via ParticleSystem pointScale).
        float minSize = 0.16f;
        float maxSize = 0.26f;

        float pointScale = 640.0f;

        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string fragShaderPath = "assets/shaders/vfx/seed_projectile.frag";

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Alpha;
        bool depthTest  = false;
        bool depthWrite = false;

        // Global acceleration (per-particle accel handles the arc).
        glm::vec3 acceleration = glm::vec3(0.0f);
        float dampingBase = 1.0f;

        float spawnRadius = 0.02f;
    };

public:
    LeechSeedProjectileVFX() = default;

    void setConfig(const Config& c) {
        cfg = c;
        configured = false;
        originNodeIndexCache.clear();
    }

    const Config& getConfig() const { return cfg; }

    void update(float dt);
    void render(const Camera3D& camera);
    const ParticleSystem& getParticles() const { return particles; }

    // travelSec is real-time seconds for the projectile to reach target.
    void emit(const PokemonInstance& attacker,
              const PokemonInstance& target,
              float travelSec);

private:
    void ensureConfigured();

    int resolveOriginNodeIndex(const Model& model) const;
    glm::mat4 computeInstanceTransform(const PokemonInstance& instance) const;
    glm::vec3 computeOriginWorld(const PokemonInstance& attacker) const;

    float rand01();
    float randRange(float a, float b);

private:
    ParticleSystem particles;
    Config cfg{};
    bool configured = false;

    engine::XorShift32 rng{0xBEEFC0DEu};

    mutable std::unordered_map<const Model*, int> originNodeIndexCache;
};
