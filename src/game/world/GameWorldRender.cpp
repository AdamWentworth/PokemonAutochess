#include "game/world/GameWorld.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"
#include "engine/render/Model.h"

#include "game/ui/HealthBarQuery.h"

void GameWorld::drawAll(const Camera3D& camera, BoardRenderer& boardRenderer) {
    lastViewMatrix = camera.getViewMatrix();
    hasLastViewMatrix = true;

    boardRenderer.draw(camera);
    boardRenderer.drawBench(camera);

    auto drawPokemonList = [&](const std::vector<PokemonInstance>& list) {
        for (const auto& instance : list) {
            if (!instance.model) continue;
            if (!instance.alive && !instance.fainting) continue;

            float scaleFactor = instance.model->getScaleFactor() *
                                std::max(0.0f, instance.modelScaleCorrection) *
                                std::max(0.0f, instance.speciesScale) *
                                std::max(0.0f, instance.visualScale) *
                                std::max(0.0f, instance.captureScale);

            glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
            glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
            glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
            glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
            glm::vec3 renderPos = instance.position + glm::vec3(0.0f, instance.visualYOffset, 0.0f);
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), renderPos);

            glm::mat4 instanceTransform = translation * rotationY * rotationX * rotationZ * scale;

            const float tintStrength = std::clamp(instance.captureTintStrength, 0.0f, 1.0f);
            instance.model->drawAnimated(camera, instanceTransform, instance.animTimeSec, instance.activeAnimIndex,
                                         glm::vec3(1.0f, 0.1f, 0.1f), tintStrength);
        }
    };

    drawPokemonList(pokemons);
    drawPokemonList(benchPokemons);

    if (pokeballModelLoaded && pokeballModel) {
        constexpr float kPokeballCaptureYawModelOffsetDeg = -90.0f;
        for (const auto& attempt : captureAttempts) {
            if (attempt.timeLeftSec <= 0.0f) continue;
            float scaleFactor = pokeballModel->getScaleFactor() * std::max(0.0f, attempt.ballScale);
            glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
            float yawDeg = attempt.ballYawDeg;
            float rollDeg = 0.0f;
            if (attempt.phase == CaptureAttempt::Phase::Shake ||
                attempt.phase == CaptureAttempt::Phase::Resolve) {
                // Keep a stable facing during roll/shake; legacy previously oscillated yaw
                // left/right which reads as the ball "flipping" instead of rocking.
                yawDeg = 0.0f;
            }
            if (attempt.phase == CaptureAttempt::Phase::Shake) {
                const int totalShakes = std::max(1, attempt.shakes);
                const float perShake = std::max(0.2f, attempt.shakeDur);
                const float totalDur = (attempt.shakes > 0) ? (perShake * attempt.shakes) : perShake;
                const float phaseNorm01 =
                    std::clamp(attempt.phaseTime / std::max(0.05f, totalDur), 0.0f, 1.0f);
                const float theta = phaseNorm01 * static_cast<float>(totalShakes) * 6.28318530718f;
                rollDeg = std::sin(theta) * 18.0f;
            }
            yawDeg += kPokeballCaptureYawModelOffsetDeg;
            glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(yawDeg), glm::vec3(0, 1, 0));
            glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(rollDeg), glm::vec3(0, 0, 1));
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), attempt.ballPos);
            glm::mat4 instanceTransform = translation * rotationY * rotationZ * scale;
            pokeballModel->drawAnimated(camera, instanceTransform, 0.0f, 0);
        }
    }

    // draw particles AFTER opaque models
    tailFireVfx.render(camera);
    grassImpactVfx.render(camera);
    tackleImpactVfx.render(camera);
    leechSeedVfx.render(camera);
    healPlusVfx.render(camera);
    leechSeedDrainVfx.render(camera);
    growlWaveVfx.render(camera);
    clawSwipeVfx.render(camera);
    aquaSwooshVfx.render(camera);
}

std::vector<HealthBarData> GameWorld::getHealthBarData(const Camera3D& camera,
                                                       int screenWidth,
                                                       int screenHeight) const {
    return BuildHealthBarData(pokemons, benchPokemons, camera, screenWidth, screenHeight, config);
}

bool GameWorld::buildGrowlWaveSnapshot(GrowlWaveVFX::RenderSnapshot& out) const {
    return growlWaveVfx.buildRenderSnapshot(out);
}

bool GameWorld::buildParticleVfxSnapshots(ParticleVfxSnapshots& out) const {
    bool any = false;
    any = tailFireVfx.getParticles().buildRenderSnapshot(out.tailFire) || any;
    any = grassImpactVfx.getParticles().buildRenderSnapshot(out.grassImpact) || any;
    any = tackleImpactVfx.getBurstParticles().buildRenderSnapshot(out.tackleBurst) || any;
    any = tackleImpactVfx.getSparkParticles().buildRenderSnapshot(out.tackleSpark) || any;
    any = leechSeedVfx.getParticles().buildRenderSnapshot(out.leechSeedProjectile) || any;
    any = healPlusVfx.getParticles().buildRenderSnapshot(out.healPlus) || any;
    any = leechSeedDrainVfx.getParticles().buildRenderSnapshot(out.leechSeedDrain) || any;
    any = clawSwipeVfx.getParticles().buildRenderSnapshot(out.clawSwipe) || any;
    any = aquaSwooshVfx.getParticles().buildRenderSnapshot(out.aquaSwoosh) || any;
    return any;
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
            // Shared capture presentation contract: keep target mostly unchanged on impact/open, then ramp
            // red/fade/suck-in near the end of absorb (requested behavior for animated pokeball sequence).
            snap.absorbLateVisual01 = smoothstep01(0.84f, 0.985f, snap.absorbNorm01);
        }
        out.push_back(std::move(snap));
    }

    return !out.empty();
}

