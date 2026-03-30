#pragma once

#include <cstdint>
#include <unordered_map>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"
#include "game/vfx/TailFireVFXConfig.h"

namespace game::runtime::shared_tail_fire_synth_emitter {

struct SyntheticEmitterState {
    ParticleSystem particles;
    bool configured = false;
    std::unordered_map<int, float> emitAccumulator;
    std::unordered_map<int, std::uint32_t> spawnSerial;
    std::unordered_map<int, glm::vec3> prevTailWorld;
    std::unordered_map<int, glm::vec3> smoothedTailWorld;
    std::unordered_map<int, int> prevAnimIndex;
    std::unordered_map<int, float> prevAnimTimeSec;
    std::unordered_map<int, glm::vec3> filteredTailVel;
};

struct EmitParticleArgs {
    int unitId = 0;
    glm::vec3 anchorWorld{0.0f};
    glm::mat3 tailBasis{1.0f};
    glm::vec3 backDirWorld{0.0f, 1.0f, 0.0f};
    glm::vec3 tailVelocity{0.0f};
    float particleScale = 1.0f;
};

float clampStepDt(float dt);
void resetState(SyntheticEmitterState& state);
void ensureConfigured(SyntheticEmitterState& state, const TailFireVFXConfig& cfg);
int beginUnitEmission(SyntheticEmitterState& state,
                      const TailFireVFXConfig& cfg,
                      int unitId,
                      float dt,
                      int animIndex,
                      float animTimeSec,
                      float emitRateScale = 1.0f);
glm::vec3 resolveSmoothedAnchor(SyntheticEmitterState& state,
                                const TailFireVFXConfig& cfg,
                                int unitId,
                                const glm::vec3& tailPosWorld,
                                float dt);
glm::vec3 resolveFilteredTailVelocity(SyntheticEmitterState& state,
                                      int unitId,
                                      const glm::vec3& tailPosWorld,
                                      float dt);
void emitParticles(SyntheticEmitterState& state,
                   const TailFireVFXConfig& cfg,
                   const EmitParticleArgs& args,
                   int emitCount);

} // namespace game::runtime::shared_tail_fire_synth_emitter
