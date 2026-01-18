// src/game/vfx/CharmanderTailFireVFX.h
#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <glm/glm.hpp>

#include "../PokemonInstance.h"
#include "engine/vfx/ParticleSystem.h"

class Camera3D;

// Charmander-specific tail fire emitter using ParticleSystem.
class CharmanderTailFireVFX {
public:
    CharmanderTailFireVFX() = default;

    void update(float dt,
                const std::vector<PokemonInstance>& boardUnits,
                const std::vector<PokemonInstance>& benchUnits);

    void render(const Camera3D& camera);

private:
    void emitForList(float dt, const std::vector<PokemonInstance>& list);

    glm::mat4 computeInstanceTransform(const PokemonInstance& instance) const;
    bool isCharmanderName(const std::string& name) const;

private:
    ParticleSystem particles;

    // per-unit accumulator so emission is stable regardless of FPS
    std::unordered_map<int, float> emitAccumulator;

    // Tunables
    float emitRatePerSec = 160.0f; // a bit denser for a cohesive flame
    float spawnRadius = 0.010f;

    // Tail node (tail_06) for your current Charmander GLTF
    int tailTipNodeIndex = 45;

    // Raise/lower relative to the tail tip node
    float tailWorldYOffset = 0.02f;
};
