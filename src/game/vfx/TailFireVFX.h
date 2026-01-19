// --- FILE: src/game/vfx/TailFireVFX.h ---
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
        float emitRatePerSec = 65.0f;
        float spawnRadius    = 0.010f;

        int   tailTipNodeIndex  = 45;
        float tailWorldYOffset  = 0.2f;

        glm::vec3 backDir = glm::vec3(0.0f, 0.0f, 1.0f);

        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string fragShaderPath = "assets/shaders/vfx/fire/fire_tail.frag";

        std::string flipbookPath = "assets/textures/fire_flipbook_8x5.png";
        int   flipbookCols = 8;
        int   flipbookRows = 5;
        int   flipbookFrames = 40;
        float flipbookFps = 30.0f;

        // Secondary atlas
        std::string flipbook2Path = "assets/textures/fire_flipbook2_8x5.png";
        int   flipbook2Cols = 8;
        int   flipbook2Rows = 5;
        int   flipbook2Frames = 40;
        float flipbook2Fps = 30.0f;
        bool  useFlipbook2 = true;

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Premultiplied;
        bool depthTest  = true;
        bool depthWrite = false;

        glm::vec3 acceleration = glm::vec3(0.0f, 1.2f, 0.0f);
        float dampingBase = 0.07f;

        float pointScale = 900.0f;

        // IMPORTANT: enable flipbooks so the new atlas actually affects the result
        bool useFlipbook = true;
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
