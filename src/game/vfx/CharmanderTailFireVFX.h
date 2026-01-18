// src/game/vfx/CharmanderTailFireVFX.h
#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <glm/glm.hpp>

#include "../PokemonInstance.h"
#include "engine/vfx/ParticleSystem.h"

class Camera3D;

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

    std::unordered_map<int, float> emitAccumulator;
    std::unordered_map<int, uint32_t> spawnSerial;

    // CHANGED: less overdraw (was ~160)
    float emitRatePerSec = 90.0f;

    float spawnRadius = 0.010f;

    int tailTipNodeIndex = 45;

    // keep your tuned offset
    float tailWorldYOffset = 0.2f;
};
