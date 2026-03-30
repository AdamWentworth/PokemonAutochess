#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSyntheticEmitter.h"

#include <algorithm>
#include <cmath>

namespace game::runtime::shared_tail_fire_synth_emitter {
namespace {

float hash01(float x) {
    const float s = std::sin(x * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

float hashSigned(float x) {
    return hash01(x) * 2.0f - 1.0f;
}

void clearUnitMotionHistory(SyntheticEmitterState& state, int unitId) {
    state.prevTailWorld.erase(unitId);
    state.smoothedTailWorld.erase(unitId);
    state.prevAnimTimeSec.erase(unitId);
    state.filteredTailVel.erase(unitId);
}

} // namespace

float clampStepDt(float dt) {
    return std::clamp(dt, 0.0f, 0.05f);
}

void resetState(SyntheticEmitterState& state) {
    state.particles.shutdown();
    state.configured = false;
    state.emitAccumulator.clear();
    state.spawnSerial.clear();
    state.prevTailWorld.clear();
    state.smoothedTailWorld.clear();
    state.prevAnimIndex.clear();
    state.prevAnimTimeSec.clear();
    state.filteredTailVel.clear();
}

void ensureConfigured(SyntheticEmitterState& state, const TailFireVFXConfig& cfg) {
    if (state.configured) return;

    state.particles.setShaderPaths(cfg.vertShaderPath, cfg.fragShaderPath);
    state.particles.setUseFlipbook(cfg.useFlipbook);
    if (cfg.useFlipbook) {
        state.particles.setFlipbook(
            cfg.flipbookPath,
            cfg.flipbookCols,
            cfg.flipbookRows,
            cfg.flipbookFrames,
            cfg.flipbookFps);

        if (cfg.useFlipbook2) {
            state.particles.setSecondaryFlipbook(
                cfg.flipbook2Path,
                cfg.flipbook2Cols,
                cfg.flipbook2Rows,
                cfg.flipbook2Frames,
                cfg.flipbook2Fps);
        } else {
            state.particles.setSecondaryFlipbook("", 1, 1, 1, 0.0f);
        }
    } else {
        state.particles.setSecondaryFlipbook("", 1, 1, 1, 0.0f);
    }

    ParticleSystem::RenderSettings renderSettings;
    renderSettings.blend = cfg.blend;
    renderSettings.depthTest = cfg.depthTest;
    renderSettings.depthWrite = cfg.depthWrite;
    renderSettings.programPointSize = true;
    state.particles.setRenderSettings(renderSettings);

    ParticleSystem::UpdateSettings updateSettings;
    updateSettings.acceleration = cfg.acceleration;
    updateSettings.dampingBase = cfg.dampingBase;
    state.particles.setUpdateSettings(updateSettings);

    state.particles.setPointScale(cfg.pointScale);
    state.configured = true;
}

int beginUnitEmission(SyntheticEmitterState& state,
                      const TailFireVFXConfig& cfg,
                      int unitId,
                      float dt,
                      int animIndex,
                      float animTimeSec,
                      float emitRateScale) {
    const bool calmerSingleFlipbook = !cfg.useFlipbook2;
    const float singleFlipbookEmitScale = calmerSingleFlipbook ? 0.45f : 1.0f;
    const float emitRatePerSec =
        std::max(0.0f, cfg.emitRatePerSec * std::max(0.0f, emitRateScale) * singleFlipbookEmitScale);
    if (emitRatePerSec <= 0.0f) return 0;

    float& acc = state.emitAccumulator[unitId];
    acc += dt * emitRatePerSec;

    const int emitCount = static_cast<int>(std::floor(acc));
    if (emitCount <= 0) return 0;
    acc -= static_cast<float>(emitCount);

    int& prevIdx = state.prevAnimIndex[unitId];
    if (prevIdx != animIndex) {
        prevIdx = animIndex;
        clearUnitMotionHistory(state, unitId);
    }

    bool timeWrapped = false;
    auto itT = state.prevAnimTimeSec.find(unitId);
    if (itT == state.prevAnimTimeSec.end()) {
        state.prevAnimTimeSec[unitId] = animTimeSec;
    } else {
        if (animTimeSec + 1e-4f < itT->second) {
            timeWrapped = true;
        }
        itT->second = animTimeSec;
    }
    if (timeWrapped) {
        clearUnitMotionHistory(state, unitId);
    }

    return emitCount;
}

glm::vec3 resolveSmoothedAnchor(SyntheticEmitterState& state,
                                const TailFireVFXConfig& cfg,
                                int unitId,
                                const glm::vec3& tailPosWorld,
                                float dt) {
    if (cfg.followSmoothing <= 0.0f) {
        return tailPosWorld;
    }

    auto it = state.smoothedTailWorld.find(unitId);
    if (it == state.smoothedTailWorld.end()) {
        it = state.smoothedTailWorld.emplace(unitId, tailPosWorld).first;
    }

    glm::vec3& smoothed = it->second;
    const float a = 1.0f - std::exp(-cfg.followSmoothing * dt);
    smoothed = (1.0f - a) * smoothed + a * tailPosWorld;
    return smoothed;
}

glm::vec3 resolveFilteredTailVelocity(SyntheticEmitterState& state,
                                      int unitId,
                                      const glm::vec3& tailPosWorld,
                                      float dt) {
    glm::vec3 tailVelocity(0.0f);
    const auto itPrev = state.prevTailWorld.find(unitId);
    if (itPrev != state.prevTailWorld.end()) {
        const glm::vec3 delta = tailPosWorld - itPrev->second;
        const float maxDeltaPerFrame = 0.20f;
        const bool discontinuity =
            glm::dot(delta, delta) > maxDeltaPerFrame * maxDeltaPerFrame;
        if (!discontinuity) {
            const float invDt = (dt > 1e-6f) ? (1.0f / dt) : 0.0f;
            glm::vec3 rawVelocity = delta * invDt;

            const float maxTailVelocity = 4.0f;
            const float speedSq = glm::dot(rawVelocity, rawVelocity);
            if (speedSq > maxTailVelocity * maxTailVelocity) {
                rawVelocity *= (maxTailVelocity / std::sqrt(speedSq));
            }

            auto itFiltered = state.filteredTailVel.find(unitId);
            if (itFiltered == state.filteredTailVel.end()) {
                itFiltered = state.filteredTailVel.emplace(unitId, rawVelocity).first;
            }

            glm::vec3& filtered = itFiltered->second;
            const float smoothingStrength = 25.0f;
            const float a = 1.0f - std::exp(-smoothingStrength * dt);
            filtered = (1.0f - a) * filtered + a * rawVelocity;
            tailVelocity = filtered;
        } else {
            state.filteredTailVel.erase(unitId);
        }
    }

    state.prevTailWorld[unitId] = tailPosWorld;
    return tailVelocity;
}

void emitParticles(SyntheticEmitterState& state,
                   const TailFireVFXConfig& cfg,
                   const EmitParticleArgs& args,
                   int emitCount) {
    if (emitCount <= 0) return;

    const bool calmerSingleFlipbook = !cfg.useFlipbook2;
    std::uint32_t& serial = state.spawnSerial[args.unitId];
    const float unitSeed = hash01(static_cast<float>(args.unitId) * 13.137f + 0.417f);

    for (int emitIndex = 0; emitIndex < emitCount; ++emitIndex) {
        const float base = static_cast<float>(serial++);
        const float jitterScale = calmerSingleFlipbook ? 0.14f : 1.0f;
        const glm::vec3 localJitter(
            hashSigned(base + 1.0f) * cfg.spawnRadius * jitterScale,
            hashSigned(base + 2.0f) * cfg.spawnRadius * jitterScale,
            hashSigned(base + 3.0f) * cfg.spawnRadius * jitterScale);

        ParticleSystem::Particle particle;
        particle.pos = args.anchorWorld + args.tailBasis * localJitter;

        const float upVelocity =
            (calmerSingleFlipbook ? 0.006f : 0.055f) +
            hash01(base + 5.0f) * (calmerSingleFlipbook ? 0.010f : 0.095f);
        const float backVelocity =
            (calmerSingleFlipbook ? 0.002f : 0.050f) +
            hash01(base + 6.0f) * (calmerSingleFlipbook ? 0.006f : 0.050f);
        particle.vel = glm::vec3(0.0f, upVelocity, 0.0f) + args.backDirWorld * backVelocity;

        const float inheritVelocity = calmerSingleFlipbook ? 0.0f : cfg.inheritVelocity;
        if (inheritVelocity != 0.0f) {
            glm::vec3 inheritedVelocity = args.tailVelocity * inheritVelocity;
            const float maxInheritedVelocity = 2.5f;
            const float inheritedSpeedSq = glm::dot(inheritedVelocity, inheritedVelocity);
            if (inheritedSpeedSq > maxInheritedVelocity * maxInheritedVelocity) {
                inheritedVelocity *= (maxInheritedVelocity / std::sqrt(inheritedSpeedSq));
            }
            particle.vel += inheritedVelocity;
        }

        particle.maxLifeSec =
            (calmerSingleFlipbook ? 0.045f : 0.14f) +
            hash01(base + 7.0f) * (calmerSingleFlipbook ? 0.020f : 0.10f);
        particle.lifeSec = particle.maxLifeSec;

        const float sizeBase = (calmerSingleFlipbook ? 0.30f : 0.22f) * args.particleScale;
        const float sizeJitter = (calmerSingleFlipbook ? 0.015f : 0.10f) * args.particleScale;
        particle.sizePx = sizeBase + hash01(base + 8.0f) * sizeJitter;
        particle.seed = calmerSingleFlipbook ? unitSeed : hash01(base + 9.0f);

        state.particles.emit(particle);
    }
}

} // namespace game::runtime::shared_tail_fire_synth_emitter
