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
class Model;

class TailFireVFX {
public:
    struct Config {
        float emitRatePerSec = 65.0f;
        float spawnRadius    = 0.010f;

        // Preferred: stable across GLB node reordering.
        std::string tailTipNodeName;

        // Optional exact fire attachment helpers. When both exist, the flipbook
        // follows the authored fire rig instead of approximating from the tail tip.
        std::string fireAnchorBaseNodeName;
        std::string fireAnchorTipNodeName;

        // Legacy fallback (keep for transition).
        int tailTipNodeIndex = 45;

        float tailWorldYOffset = 0.2f;

        // Interpreted as a LOCAL direction in the tail node's frame.
        // Example "0,0,1" means "forward" in the tail bone frame.
        glm::vec3 backDir = glm::vec3(0.0f, 0.0f, 1.0f);

        // How much tail tip linear velocity gets inherited by particles.
        // 0 = none, 1 = fully inherit.
        float inheritVelocity = 0.9f;

        // Exponential smoothing strength for the emission anchor (0 disables).
        // Higher = follows more tightly; lower = smoother/laggier.
        float followSmoothing = 0.0f;

        std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
        std::string fragShaderPath = "assets/shaders/vfx/fire/fire_tail.frag";

        std::string flipbookPath = "assets/textures/fire_flipbook_8x5.png";
        int flipbookCols = 8;
        int flipbookRows = 5;
        int flipbookFrames = 40;
        float flipbookFps = 30.0f;

        std::string flipbook2Path = "assets/textures/fire_flipbook2_8x5.png";
        int flipbook2Cols = 8;
        int flipbook2Rows = 5;
        int flipbook2Frames = 40;
        float flipbook2Fps = 30.0f;
        bool useFlipbook2 = true;

        ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Premultiplied;
        bool depthTest  = true;
        bool depthWrite = false;

        glm::vec3 acceleration = glm::vec3(0.0f, 1.2f, 0.0f);
        float dampingBase = 0.07f;

        float pointScale = 900.0f;
        bool useFlipbook = true;
    };

public:
    TailFireVFX() = default;

    void setFilter(std::function<bool(const PokemonInstance&)> f) { filter = std::move(f); }
    void setNameFilterCaseInsensitive(const std::string& nameLowerOrAnyCase);

    void setConfig(const Config& c) {
        cfg = c;
        configured = false;

        // important if node name changes
        tailNodeIndexCache.clear();
        fireAnchorBaseNodeIndexCache.clear();
        fireAnchorTipNodeIndexCache.clear();

        // motion history depends on attachment points + config feel
        prevTailWorld.clear();
        smoothedTailWorld.clear();
        prevAnimIndex.clear();

        // NEW (wrap + filtering)
        prevAnimTimeSec.clear();
        filteredTailVel.clear();
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

    // Resolve name->index once per Model* (cached). Uses cfg.tailTipNodeIndex as fallback.
    int resolveTailTipNodeIndex(const Model& model) const;
    int resolveOptionalNodeIndex(const Model& model,
                                 const std::string& nodeName,
                                 std::unordered_map<const Model*, int>& cache) const;

private:
    ParticleSystem particles;

    std::function<bool(const PokemonInstance&)> filter;

    std::unordered_map<int, float> emitAccumulator;
    std::unordered_map<int, uint32_t> spawnSerial;

    // per-unit tail motion history
    std::unordered_map<int, glm::vec3> prevTailWorld;
    std::unordered_map<int, glm::vec3> smoothedTailWorld;

    // detect animation switches to prevent bogus tail velocity spikes
    std::unordered_map<int, int> prevAnimIndex;

    // NEW: detect anim time wrap for looping clips
    std::unordered_map<int, float> prevAnimTimeSec;

    // NEW: low-pass filtered tail velocity to avoid micro-spikes
    std::unordered_map<int, glm::vec3> filteredTailVel;

    Config cfg{};
    bool configured = false;

    mutable std::unordered_map<const Model*, int> tailNodeIndexCache;
    mutable std::unordered_map<const Model*, int> fireAnchorBaseNodeIndexCache;
    mutable std::unordered_map<const Model*, int> fireAnchorTipNodeIndexCache;
};
