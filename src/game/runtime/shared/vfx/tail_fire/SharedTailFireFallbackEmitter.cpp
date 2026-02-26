#include "game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.h"

#include "game/runtime/BackendWorldProxyGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace game::runtime::shared_tail_fire_fallback {
namespace {

std::string toLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
        return static_cast<char>(c);
    });
    return s;
}

struct EmitterState {
    ParticleSystem particles;
    bool configured = false;
    double lastSimTimeSec = -1.0;
    std::unordered_map<int, float> emitAccumulator;
    std::unordered_map<int, std::uint32_t> spawnSerial;
    std::unordered_map<int, glm::vec3> prevTailWorld;
    std::unordered_map<int, glm::vec3> smoothedTailWorld;
    std::unordered_map<int, int> prevAnimIndex;
    std::unordered_map<int, float> prevAnimTimeSec;
    std::unordered_map<int, glm::vec3> filteredTailVel;
};

thread_local EmitterState gState;

void resetState() {
    gState.particles.shutdown();
    gState.configured = false;
    gState.lastSimTimeSec = -1.0;
    gState.emitAccumulator.clear();
    gState.spawnSerial.clear();
    gState.prevTailWorld.clear();
    gState.smoothedTailWorld.clear();
    gState.prevAnimIndex.clear();
    gState.prevAnimTimeSec.clear();
    gState.filteredTailVel.clear();
}

void ensureConfigured(const TailFireVFX::Config& cfg) {
    if (gState.configured) return;
    gState.particles.setShaderPaths(cfg.vertShaderPath, cfg.fragShaderPath);
    gState.particles.setUseFlipbook(cfg.useFlipbook);
    if (cfg.useFlipbook) {
        gState.particles.setFlipbook(
            cfg.flipbookPath, cfg.flipbookCols, cfg.flipbookRows, cfg.flipbookFrames, cfg.flipbookFps);
        if (cfg.useFlipbook2) {
            gState.particles.setSecondaryFlipbook(
                cfg.flipbook2Path,
                cfg.flipbook2Cols,
                cfg.flipbook2Rows,
                cfg.flipbook2Frames,
                cfg.flipbook2Fps);
        } else {
            gState.particles.setSecondaryFlipbook("", 1, 1, 1, 0.0f);
        }
    } else {
        gState.particles.setSecondaryFlipbook("", 1, 1, 1, 0.0f);
    }

    ParticleSystem::RenderSettings rs;
    rs.blend = cfg.blend;
    rs.depthTest = cfg.depthTest;
    rs.depthWrite = cfg.depthWrite;
    rs.programPointSize = true;
    gState.particles.setRenderSettings(rs);

    ParticleSystem::UpdateSettings us;
    us.acceleration = cfg.acceleration;
    us.dampingBase = cfg.dampingBase;
    gState.particles.setUpdateSettings(us);
    gState.particles.setPointScale(cfg.pointScale);
    gState.configured = true;
}

float hash01(float x) {
    const float s = std::sin(x * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

float hashSigned(float x) {
    return hash01(x) * 2.0f - 1.0f;
}

glm::vec3 safeNormOr(glm::vec3 v, const glm::vec3& fallback) {
    const float len2 = glm::dot(v, v);
    if (len2 <= 1e-10f) return fallback;
    return v * (1.0f / std::sqrt(len2));
}

void emitForList(
    float dt,
    const std::vector<PokemonInstance>& list,
    const TailFireVFX::Config& cfg,
    float worldCellSize,
    const std::unordered_map<int, Anchor>* anchors) {
    dt = std::clamp(dt, 0.0f, 0.05f);
    if (dt <= 0.0f) return;

    for (const auto& unit : list) {
        if (toLowerAscii(unit.name) != "charmander") continue;
        if (!unit.alive) continue;

        float& acc = gState.emitAccumulator[unit.id];
        acc += dt * cfg.emitRatePerSec;
        int emitCount = static_cast<int>(std::floor(acc));
        if (emitCount <= 0) continue;
        acc -= static_cast<float>(emitCount);

        int animIdx = unit.activeAnimIndex;
        if (animIdx < 0) animIdx = unit.animIdleIndex;

        int& prevIdx = gState.prevAnimIndex[unit.id];
        if (prevIdx != animIdx) {
            prevIdx = animIdx;
            gState.prevTailWorld.erase(unit.id);
            gState.smoothedTailWorld.erase(unit.id);
            gState.prevAnimTimeSec.erase(unit.id);
            gState.filteredTailVel.erase(unit.id);
        }

        bool timeWrapped = false;
        auto itT = gState.prevAnimTimeSec.find(unit.id);
        if (itT == gState.prevAnimTimeSec.end()) {
            gState.prevAnimTimeSec[unit.id] = unit.animTimeSec;
        } else {
            const float prevT = itT->second;
            if (unit.animTimeSec + 1e-4f < prevT) timeWrapped = true;
            itT->second = unit.animTimeSec;
        }
        if (timeWrapped) {
            gState.prevTailWorld.erase(unit.id);
            gState.smoothedTailWorld.erase(unit.id);
            gState.filteredTailVel.erase(unit.id);
        }

        const auto extents = game::runtime::backend_proxy::computeUnitProxyExtents(unit, worldCellSize);
        if (extents.height <= 0.0001f) continue;

        const auto anchorIt = anchors ? anchors->find(unit.id) : std::unordered_map<int, Anchor>::const_iterator{};
        const bool hasTailAnchor = anchors && (anchorIt != anchors->end()) && anchorIt->second.valid;
        const Anchor tailAnchorData = hasTailAnchor ? anchorIt->second : Anchor{};

        const glm::vec3 center = unit.position + glm::vec3(0.0f, unit.visualYOffset, 0.0f);
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        const glm::vec3 fwd = game::runtime::backend_proxy::yawForward(unit.rotation.y);
        const glm::vec3 right = game::runtime::backend_proxy::yawRight(unit.rotation.y);
        const float scaleMul = std::clamp(extents.height / std::max(0.05f, worldCellSize * 0.72f), 0.80f, 2.40f);
        const float spawnRadius = std::max(
            0.004f,
            cfg.spawnRadius * (hasTailAnchor ? tailAnchorData.particleSizeScale : scaleMul));
        const float tailBackOffset =
            std::max(0.03f, extents.halfDepth * 0.82f + cfg.spawnRadius * 2.5f);
        const glm::vec3 proxyTailDir = safeNormOr((-fwd * 0.85f) + (up * 0.52f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 tailPosWorld = hasTailAnchor
            ? tailAnchorData.pos
            : (center - fwd * tailBackOffset +
               up * std::max(0.02f, cfg.tailWorldYOffset) +
               proxyTailDir * std::max(0.003f, spawnRadius * 0.8f));
        const glm::mat3 tailBasis = hasTailAnchor ? tailAnchorData.basis : glm::mat3(right, up, fwd);
        glm::vec3 backDirWorld = hasTailAnchor ? tailAnchorData.backDir : proxyTailDir;
        backDirWorld = safeNormOr(backDirWorld, glm::vec3(0.0f, 1.0f, 0.0f));

        glm::vec3 anchor = tailPosWorld;
        if (cfg.followSmoothing > 0.0f) {
            auto itS = gState.smoothedTailWorld.find(unit.id);
            if (itS == gState.smoothedTailWorld.end()) {
                gState.smoothedTailWorld[unit.id] = tailPosWorld;
            }
            glm::vec3& s = gState.smoothedTailWorld[unit.id];
            const float a = 1.0f - std::exp(-cfg.followSmoothing * dt);
            s = (1.0f - a) * s + a * tailPosWorld;
            anchor = s;
        }

        glm::vec3 tailVel(0.0f);
        auto itPrev = gState.prevTailWorld.find(unit.id);
        if (itPrev != gState.prevTailWorld.end()) {
            const glm::vec3 prev = itPrev->second;
            const glm::vec3 delta = (tailPosWorld - prev);
            const float maxDeltaPerFrame = 0.20f;
            const bool discontinuity = (glm::dot(delta, delta) > maxDeltaPerFrame * maxDeltaPerFrame);
            if (!discontinuity) {
                const float invDt = (dt > 1e-6f) ? (1.0f / dt) : 0.0f;
                glm::vec3 rawVel = delta * invDt;
                const float maxTailVel = 4.0f;
                const float sp2 = glm::dot(rawVel, rawVel);
                if (sp2 > maxTailVel * maxTailVel) {
                    rawVel *= (maxTailVel / std::sqrt(sp2));
                }
                auto itFilt = gState.filteredTailVel.find(unit.id);
                if (itFilt == gState.filteredTailVel.end()) {
                    gState.filteredTailVel[unit.id] = rawVel;
                }
                glm::vec3& vFilt = gState.filteredTailVel[unit.id];
                const float k = 25.0f;
                const float a = 1.0f - std::exp(-k * dt);
                vFilt = (1.0f - a) * vFilt + a * rawVel;
                tailVel = vFilt;
            } else {
                gState.filteredTailVel.erase(unit.id);
            }
        }
        gState.prevTailWorld[unit.id] = tailPosWorld;

        std::uint32_t& serial = gState.spawnSerial[unit.id];
        for (int k = 0; k < emitCount; ++k) {
            const float base = static_cast<float>(serial++);
            const glm::vec3 localJitter(
                hashSigned(base + 1.0f) * spawnRadius,
                hashSigned(base + 2.0f) * spawnRadius,
                hashSigned(base + 3.0f) * spawnRadius);
            const glm::vec3 worldJitter = tailBasis * localJitter;

            ParticleSystem::Particle p;
            p.pos = anchor + worldJitter;

            const float upVel = 0.055f + hash01(base + 5.0f) * 0.095f;
            const float backVel = 0.050f + hash01(base + 6.0f) * 0.050f;
            p.vel = glm::vec3(0.0f, upVel, 0.0f) + backDirWorld * backVel;

            if (cfg.inheritVelocity != 0.0f) {
                glm::vec3 inh = tailVel * cfg.inheritVelocity;
                const float maxInherit = 2.5f;
                const float inh2 = glm::dot(inh, inh);
                if (inh2 > maxInherit * maxInherit) {
                    inh *= (maxInherit / std::sqrt(inh2));
                }
                p.vel += inh;
            }

            p.maxLifeSec = 0.14f + hash01(base + 7.0f) * 0.10f;
            p.lifeSec = p.maxLifeSec;
            const float sizeScale = hasTailAnchor ? tailAnchorData.particleSizeScale : scaleMul;
            p.sizePx = (0.22f + hash01(base + 8.0f) * 0.10f) * sizeScale;
            p.seed = hash01(base + 9.0f);
            gState.particles.emit(p);
        }
    }
}

} // namespace

bool appendSyntheticTailFire(const Args& args) {
    if (!args.cfg || !args.pokemons || !args.benchPokemons || !args.appendSnapshot) return false;

    ensureConfigured(*args.cfg);

    double simNowSec = args.simNowSec;
    if (!std::isfinite(simNowSec)) simNowSec = 0.0;
    if (gState.lastSimTimeSec < 0.0 ||
        simNowSec + 1e-6 < gState.lastSimTimeSec ||
        (simNowSec - gState.lastSimTimeSec) > 2.0) {
        resetState();
        ensureConfigured(*args.cfg);
        gState.lastSimTimeSec = simNowSec;
    }
    double simDeltaSec = simNowSec - gState.lastSimTimeSec;
    if (!std::isfinite(simDeltaSec) || simDeltaSec < 0.0) simDeltaSec = 0.0;
    simDeltaSec = std::min(simDeltaSec, 0.50);
    gState.lastSimTimeSec = simNowSec;

    while (simDeltaSec > 1e-6) {
        const float step = static_cast<float>(std::min(simDeltaSec, 0.05));
        gState.particles.update(step);
        emitForList(step, *args.pokemons, *args.cfg, args.worldCellSize, args.anchors);
        emitForList(step, *args.benchPokemons, *args.cfg, args.worldCellSize, args.anchors);
        simDeltaSec -= static_cast<double>(step);
    }

    ParticleSystem::RenderSnapshot syntheticTailFire;
    if (!gState.particles.buildRenderSnapshot(syntheticTailFire)) return false;
    syntheticTailFire.timeSec = static_cast<float>(simNowSec);
    return args.appendSnapshot("tail_fire_synth", syntheticTailFire);
}

} // namespace game::runtime::shared_tail_fire_fallback

