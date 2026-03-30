#include "game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.h"

#include "engine/core/Environment.h"
#include "game/runtime/render_prep/WorldProxyGeometry.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireAnchorMath.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSyntheticEmitter.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace anchor_math = game::runtime::shared_tail_fire_anchor_math;
namespace synth_emitter = game::runtime::shared_tail_fire_synth_emitter;

namespace game::runtime::shared_tail_fire_fallback {
namespace {

float tailFireFallbackEmitScale() {
    static const float kValue = []() {
        const auto env = engine::env::get("PAC_BACKEND_TAIL_FIRE_FALLBACK_EMIT_SCALE");
        if (!env || env->empty()) return 0.65f;
        try {
            return std::clamp(std::stof(*env), 0.10f, 2.00f);
        } catch (...) {
            return 0.65f;
        }
    }();
    return kValue;
}

float tailFireFallbackSizeScale() {
    static const float kValue = []() {
        const auto env = engine::env::get("PAC_BACKEND_TAIL_FIRE_FALLBACK_SIZE_SCALE");
        if (!env || env->empty()) return 1.10f;
        try {
            return std::clamp(std::stof(*env), 0.50f, 3.00f);
        } catch (...) {
            return 1.10f;
        }
    }();
    return kValue;
}

bool isCharmanderName(const std::string& s) {
    if (s.size() != 10u) return false;
    return (s[0] == 'c' || s[0] == 'C') &&
           (s[1] == 'h' || s[1] == 'H') &&
           (s[2] == 'a' || s[2] == 'A') &&
           (s[3] == 'r' || s[3] == 'R') &&
           (s[4] == 'm' || s[4] == 'M') &&
           (s[5] == 'a' || s[5] == 'A') &&
           (s[6] == 'n' || s[6] == 'N') &&
           (s[7] == 'd' || s[7] == 'D') &&
           (s[8] == 'e' || s[8] == 'E') &&
           (s[9] == 'r' || s[9] == 'R');
}

struct EmitterState {
    synth_emitter::SyntheticEmitterState synth;
    ParticleSystem::RenderSnapshot snapshot;
    double lastSimTimeSec = -1.0;
};

thread_local EmitterState gState;

void resetState() {
    synth_emitter::resetState(gState.synth);
    gState.lastSimTimeSec = -1.0;
}

bool hasLiveCharmander(const std::vector<PokemonInstance>& list) {
    for (const auto& unit : list) {
        if (!unit.alive) continue;
        if (isCharmanderName(unit.name)) return true;
    }
    return false;
}

void emitForList(float dt,
                 const std::vector<PokemonInstance>& list,
                 const TailFireVFXConfig& cfg,
                 float worldCellSize,
                 const std::unordered_map<int, Anchor>* anchors) {
    dt = synth_emitter::clampStepDt(dt);
    if (dt <= 0.0f) return;

    const float emitScale = tailFireFallbackEmitScale();
    const float fallbackSizeScale = tailFireFallbackSizeScale();
    const bool hasExactFireAnchorNodes =
        !cfg.fireAnchorBaseNodeName.empty() &&
        !cfg.fireAnchorTipNodeName.empty();

    for (const auto& unit : list) {
        if (!unit.alive) continue;
        if (!isCharmanderName(unit.name)) continue;

        int animIdx = unit.activeAnimIndex;
        if (animIdx < 0) animIdx = unit.animIdleIndex;

        const int emitCount = synth_emitter::beginUnitEmission(
            gState.synth,
            cfg,
            unit.id,
            dt,
            animIdx,
            unit.animTimeSec,
            emitScale);
        if (emitCount <= 0) {
            continue;
        }

        const auto extents =
            game::runtime::render_prep_proxy::computeUnitProxyExtents(unit, worldCellSize);
        if (extents.height <= 0.0001f) continue;

        const auto anchorIt =
            anchors ? anchors->find(unit.id) : std::unordered_map<int, Anchor>::const_iterator{};
        const bool hasTailAnchor =
            anchors && (anchorIt != anchors->end()) && anchorIt->second.valid;
        const Anchor tailAnchorData = hasTailAnchor ? anchorIt->second : Anchor{};
        if (hasTailAnchor && tailAnchorData.meshCarrierActive) {
            continue;
        }

        const glm::vec3 center = unit.position + glm::vec3(0.0f, unit.visualYOffset, 0.0f);
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        const glm::vec3 forward = game::runtime::render_prep_proxy::yawForward(unit.rotation.y);
        const glm::vec3 right = game::runtime::render_prep_proxy::yawRight(unit.rotation.y);
        const float scaleMul =
            std::clamp(extents.height / std::max(0.05f, worldCellSize * 0.72f), 0.80f, 2.40f);
        const float spawnRadius =
            std::max(0.004f,
                     cfg.spawnRadius * (hasTailAnchor ? tailAnchorData.particleSizeScale : scaleMul));
        const float tailBackOffset =
            std::max(0.03f, extents.halfDepth * 0.82f + cfg.spawnRadius * 2.5f);
        const glm::vec3 proxyTailDir =
            anchor_math::safeNormOr((-forward * 0.85f) + (up * 0.52f),
                                    glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 tailPosWorld =
            hasTailAnchor
                ? (tailAnchorData.pos +
                   (hasExactFireAnchorNodes ? glm::vec3(0.0f)
                                            : glm::vec3(0.0f, cfg.tailWorldYOffset, 0.0f)))
                : (center - forward * tailBackOffset +
                   up * std::max(0.02f, cfg.tailWorldYOffset) +
                   proxyTailDir * std::max(0.003f, spawnRadius * 0.8f));
        const glm::mat3 tailBasis =
            hasTailAnchor ? tailAnchorData.basis : glm::mat3(right, up, forward);
        glm::vec3 backDirWorld =
            hasTailAnchor ? tailAnchorData.backDir : proxyTailDir;
        backDirWorld =
            anchor_math::safeNormOr(backDirWorld, glm::vec3(0.0f, 1.0f, 0.0f));

        const glm::vec3 anchorWorld =
            synth_emitter::resolveSmoothedAnchor(gState.synth, cfg, unit.id, tailPosWorld, dt);
        const glm::vec3 tailVelocity =
            synth_emitter::resolveFilteredTailVelocity(gState.synth, unit.id, tailPosWorld, dt);
        const float particleScale =
            (hasTailAnchor ? tailAnchorData.particleSizeScale : scaleMul) * fallbackSizeScale;

        synth_emitter::emitParticles(
            gState.synth,
            cfg,
            {
                .unitId = unit.id,
                .anchorWorld = anchorWorld,
                .tailBasis = tailBasis,
                .backDirWorld = backDirWorld,
                .tailVelocity = tailVelocity,
                .particleScale = particleScale,
            },
            emitCount);
    }
}

} // namespace

bool appendSyntheticTailFire(const Args& args) {
    if (!args.cfg || !args.pokemons || !args.benchPokemons || !args.appendSnapshot) return false;

    synth_emitter::ensureConfigured(gState.synth, *args.cfg);

    double simNowSec = args.simNowSec;
    if (!std::isfinite(simNowSec)) simNowSec = 0.0;
    const bool hasLiveSource =
        hasLiveCharmander(*args.pokemons) || hasLiveCharmander(*args.benchPokemons);
    if (!hasLiveSource && gState.synth.particles.particleCount() == 0u) {
        gState.lastSimTimeSec = simNowSec;
        return false;
    }
    if (gState.lastSimTimeSec < 0.0 ||
        simNowSec + 1e-6 < gState.lastSimTimeSec ||
        (simNowSec - gState.lastSimTimeSec) > 2.0) {
        resetState();
        synth_emitter::ensureConfigured(gState.synth, *args.cfg);
        gState.lastSimTimeSec = simNowSec;
    }
    double simDeltaSec = simNowSec - gState.lastSimTimeSec;
    if (!std::isfinite(simDeltaSec) || simDeltaSec < 0.0) simDeltaSec = 0.0;
    simDeltaSec = std::min(simDeltaSec, 0.50);
    gState.lastSimTimeSec = simNowSec;

    while (simDeltaSec > 1e-6) {
        const float step = static_cast<float>(std::min(simDeltaSec, 0.05));
        gState.synth.particles.update(step);
        emitForList(step, *args.pokemons, *args.cfg, args.worldCellSize, args.anchors);
        emitForList(step, *args.benchPokemons, *args.cfg, args.worldCellSize, args.anchors);
        simDeltaSec -= static_cast<double>(step);
    }

    if (!gState.synth.particles.buildRenderSnapshot(gState.snapshot)) return false;
    gState.snapshot.timeSec = static_cast<float>(simNowSec);
    return args.appendSnapshot("tail_fire_synth", gState.snapshot);
}

} // namespace game::runtime::shared_tail_fire_fallback
