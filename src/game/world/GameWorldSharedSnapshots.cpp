#include "game/world/GameWorld.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/constants.hpp>

bool GameWorld::buildGrowlWaveSnapshot(GrowlWaveVFX::RenderSnapshot& out) const {
    return growlWaveVfx.buildRenderSnapshot(out);
}

std::uint32_t GameWorld::countActiveGrowlWaveVfx() const {
    return growlWaveVfx.activeRingCount();
}

bool GameWorld::buildTackleSmokeSnapshot(TackleSmokeVFX::RenderSnapshot& out) const {
    return tackleSmokeVfx.buildRenderSnapshot(out);
}

std::uint32_t GameWorld::countActiveTackleSmokeVfx() const {
    return tackleSmokeVfx.activeCloudCount();
}

bool GameWorld::buildScratchGlowSnapshot(ScratchGlowVFX::RenderSnapshot& out) const {
    return scratchGlowVfx.buildRenderSnapshot(out);
}

std::uint32_t GameWorld::countActiveScratchGlowVfx() const {
    return scratchGlowVfx.activeGlowCount();
}

bool GameWorld::buildParticleVfxSnapshots(ParticleVfxSnapshots& out) const {
    const auto buildIfActive = [](const ParticleSystem& system,
                                  ParticleSystem::RenderSnapshot& snapshot) -> bool {
        if (system.particleCount() == 0u) {
            snapshot = ParticleSystem::RenderSnapshot{};
            return false;
        }
        return system.buildRenderSnapshot(snapshot);
    };

    bool any = false;
    any = buildIfActive(grassImpactVfx.getParticles(), out.grassImpact) || any;
    out.tackleBurst = ParticleSystem::RenderSnapshot{};
    out.tackleSpark = ParticleSystem::RenderSnapshot{};
    any = buildIfActive(leechSeedVfx.getParticles(), out.leechSeedProjectile) || any;
    any = buildIfActive(healPlusVfx.getParticles(), out.healPlus) || any;
    any = buildIfActive(leechSeedDrainVfx.getParticles(), out.leechSeedDrain) || any;
    any = buildIfActive(clawSwipeVfx.getParticles(), out.clawSwipe) || any;
    any = buildIfActive(aquaSwooshVfx.getParticles(), out.aquaSwoosh) || any;
    return any;
}

std::uint32_t GameWorld::countActiveParticleVfx() const {
    const std::size_t total =
        grassImpactVfx.getParticles().particleCount() +
        leechSeedVfx.getParticles().particleCount() +
        healPlusVfx.getParticles().particleCount() +
        leechSeedDrainVfx.getParticles().particleCount() +
        clawSwipeVfx.getParticles().particleCount() +
        aquaSwooshVfx.getParticles().particleCount();
    const std::size_t capped =
        std::min(total, static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()));
    return static_cast<std::uint32_t>(capped);
}

std::uint32_t GameWorld::countActiveCaptureAttempts() const {
    std::size_t total = 0u;
    for (const auto& attempt : captureAttempts) {
        if (attempt.timeLeftSec > 0.0f) {
            ++total;
        }
    }
    const std::size_t capped =
        std::min(total, static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()));
    return static_cast<std::uint32_t>(capped);
}

bool GameWorld::buildCaptureAttemptRenderSnapshots(std::vector<CaptureAttemptRenderSnapshot>& out) const {
    out.clear();
    if (captureAttempts.empty()) return false;
    out.reserve(captureAttempts.size());

    const auto normalize01 = [](float timeSec, float durSec) {
        if (durSec <= 0.0f) return 0.0f;
        return std::clamp(timeSec / durSec, 0.0f, 1.0f);
    };
    const auto smoothstep01 = [](float a, float b, float x) {
        if (!(b > a)) return (x >= b) ? 1.0f : 0.0f;
        const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const auto yawDegFromDirXZ = [](const glm::vec3& dir) {
        const glm::vec2 xz(dir.x, dir.z);
        const float lenSq = glm::dot(xz, xz);
        if (!(lenSq > 1e-8f)) return 0.0f;
        return glm::degrees(std::atan2(dir.x, dir.z));
    };

    for (const auto& attempt : captureAttempts) {
        if (attempt.timeLeftSec <= 0.0f) continue;
        CaptureAttemptRenderSnapshot snap;
        snap.targetId = attempt.targetId;
        snap.success = attempt.success;
        snap.shakes = attempt.shakes;
        snap.shakesEmitted = attempt.shakesEmitted;
        snap.ballPos = attempt.ballPos;
        snap.ballScale = attempt.ballScale;
        snap.ballYawDeg = attempt.ballYawDeg;
        snap.ballFacingYawDeg = yawDegFromDirXZ(attempt.startPos - attempt.targetPos);
        snap.phaseTimeSec = attempt.phaseTime;
        snap.timeLeftSec = attempt.timeLeftSec;
        switch (attempt.phase) {
        case CaptureAttempt::Phase::Throw:
            snap.phase = 0;
            snap.phaseNorm01 = normalize01(attempt.phaseTime, std::max(0.05f, attempt.throwDur));
            break;
        case CaptureAttempt::Phase::Absorb:
            snap.phase = 1;
            snap.phaseNorm01 = normalize01(attempt.phaseTime, std::max(0.05f, attempt.absorbDur));
            break;
        case CaptureAttempt::Phase::Shake:
            snap.phase = 2;
            {
                const int totalShakes = std::max(0, attempt.shakes);
                const float perShake = std::max(0.2f, attempt.shakeDur);
                const float totalDur = (totalShakes > 0) ? (perShake * totalShakes) : perShake;
                snap.phaseNorm01 = normalize01(attempt.phaseTime, totalDur);
            }
            break;
        case CaptureAttempt::Phase::Resolve:
            snap.phase = 3;
            snap.phaseNorm01 = normalize01(attempt.phaseTime, std::max(0.05f, attempt.resolveDur));
            break;
        }
        if (snap.phase == 1) {
            snap.absorbNorm01 = snap.phaseNorm01;
            snap.absorbLateVisual01 = smoothstep01(0.84f, 0.985f, snap.absorbNorm01);
        }
        out.push_back(std::move(snap));
    }

    return !out.empty();
}
