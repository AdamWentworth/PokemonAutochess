// src/game/vfx/TailFireVFX.h
#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <cstdint>
#include <glm/glm.hpp>

#include "game/PokemonInstance.h"
#include "engine/vfx/ParticleSystem.h"

class Camera3D;

class TailFireVFX {
public:
    struct Config {
        // ---------- Emission tuning ----------
        float emitRatePerSec = 65.0f;
        float spawnRadius    = 0.010f;

        // ---------- Attachment ----------
        int   tailTipNodeIndex  = 45;
        float tailWorldYOffset  = 0.2f;

        // ---------- Spawn motion ----------
        glm::vec3 backDir = glm::vec3(0.0f, 0.0f, 1.0f);

        // ---------- Particle visuals / system ----------
        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        // switch to procedural fragment shader
        std::string fragShaderPath = "assets/shaders/vfx/fire/fire_tail.frag";

        std::string flipbookPath = "assets/textures/fire_flipbook_8x5.png";
        int flipbookCols = 8;
        int flipbookRows = 5;
        int flipbookFrames = 40;
        float flipbookFps = 30.0f;

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Premultiplied;
        bool depthTest  = true;
        bool depthWrite = false;

        glm::vec3 acceleration = glm::vec3(0.0f, 1.2f, 0.0f);
        float dampingBase = 0.07f;

        // ✅ restore original look scale
        float pointScale = 900.0f;

        // disable flipbook usage (no PNG dependency)
        bool useFlipbook = false;
    };

public:
    TailFireVFX() = default;

    void setFilter(std::function<bool(const PokemonInstance&)> f) { filter = std::move(f); }
    void setNameFilterCaseInsensitive(const std::string& nameLowerOrAnyCase);

    void setConfig(const Config& c) {
        cfg = c;
        configured = false;
    }
    const Config& getConfig() const { return cfg; }

    ParticleSystem& getParticles() { return particles; }
    const ParticleSystem& getParticles() const { return particles; }

    void update(float dt,
                const std::vector<PokemonInstance>& boardUnits,
                const std::vector<PokemonInstance>& benchUnits);

    void render(const Camera3D& camera);

private:
    void ensureConfigured();
    void emitForList(float dt, const std::vector<PokemonInstance>& list);
    glm::mat4 computeInstanceTransform(const PokemonInstance& instance) const;

private:
    ParticleSystem particles;

    std::function<bool(const PokemonInstance&)> filter;

    std::unordered_map<int, float> emitAccumulator;
    std::unordered_map<int, uint32_t> spawnSerial;

    Config cfg{};
    bool configured = false;
};
