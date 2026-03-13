#include "game/runtime/shared/projected/SharedProjectedUnitOverlays.h"

#include "game/runtime/render_prep/UnitVisuals.h"
#include "game/world/MoveImpactRouting.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace {
std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}
} // namespace

namespace game::runtime::shared_projected_unit_overlays {

void appendProjectedUnitOverlays(const Args& args) {
    if (!args.unit || !args.gameWorld || !args.projectedDebug || !args.worldQuads ||
        !args.lines || !args.textLines || !args.sprites || !args.sharedUnitHudCfg) {
        return;
    }

    const PokemonInstance& unit = *args.unit;
    auto& projectedDebug = *args.projectedDebug;
    auto& worldQuads = *args.worldQuads;
    auto& lines = *args.lines;
    auto& textLines = *args.textLines;
    auto& sprites = *args.sprites;
    const auto& sharedUnitHudCfg = *args.sharedUnitHudCfg;
    GameWorld* gameWorld = args.gameWorld;

    const float cx = args.cx;
    const float cy = args.cy;
    const float unitSize = args.unitSize;
    const float minDim = args.minDim;
    const float cellPx = args.cellPx;
    const float line = args.lineThickness;
    const float worldCellSize = args.worldCellSize;
    const glm::vec3 proxyCenter = args.proxyCenter;
    const auto& extents = args.extents;
    const float animYaw = args.animYaw;

    const bool shouldShowPortrait = runtime::render_prep_units::shouldRenderWorldUnitPortrait(
        args.drewModelMesh,
        args.forcePortraitOverlay,
        args.allowPortraitFallback);
    if (shouldShowPortrait) {
        const std::string unitImagePath =
            runtime::render_prep_units::resolveWorldUnitImagePath(unit.name);
        IRenderBackend::DebugSprite unitSprite =
            runtime::render_prep_units::makeWorldUnitSprite(
                cx,
                cy,
                unitSize * 1.20f,
                unitSize * 1.20f,
                unitImagePath,
                unit.alive ? 0.96f : 0.76f);
        if (!unitSprite.texturePath.empty()) {
            sprites.push_back(std::move(unitSprite));
        }
    }

    std::string routeMoveLower = toLowerCopy(unit.activeAttackMoveName);
    if (routeMoveLower.empty()) {
        routeMoveLower = toLowerCopy(unit.pendingDamageMoveName);
    }
    const MoveImpactRoute impactRoute = classifyMoveImpactRoute(routeMoveLower);
    const bool pendingGrowl = (impactRoute == MoveImpactRoute::GrowlSoundRings);
    const bool pendingClaw = (impactRoute == MoveImpactRoute::ClawSwipe);
    const bool pendingAqua = (impactRoute == MoveImpactRoute::AquaSwoosh);
    const bool pendingLeechSeed =
        routeMoveLower.find("leech") != std::string::npos ||
        routeMoveLower.find("seed") != std::string::npos;
    const bool pendingGrass = (impactRoute == MoveImpactRoute::GrassImpact) || pendingLeechSeed;
    const glm::vec3 forward = game::runtime::render_prep_proxy::yawForward(animYaw);
    const glm::vec3 right = game::runtime::render_prep_proxy::yawRight(animYaw);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);

    if (!args.useLegacyParticleVfxSnapshotBridge) {
        projectedDebug.appendProjectedTailFire(
            unit, proxyCenter, extents, animYaw, std::max(1.0f, line * 0.92f));
        projectedDebug.appendProjectedLeechDrain(
            gameWorld,
            unit,
            std::max(0.12f, worldCellSize * 0.24f),
            std::max(1.0f, line));
    }

    if (pendingGrowl && unit.attackTimerSec > 0.0f && !args.useLegacyGrowlWaveVfx) {
        const float safeAttackDur = std::max(0.001f, unit.attackDurationSec);
        const float attackProgress =
            std::clamp(unit.animTimeSec / safeAttackDur, 0.0f, 1.0f);
        const glm::vec3 growlSource =
            proxyCenter +
            up * std::max(0.10f, extents.height * 0.32f) +
            forward * std::max(0.03f, extents.halfDepth * 0.56f);
        const float baseRadius = std::max(0.04f, extents.halfWidth * 0.62f);
        const float ringAlpha = 0.42f + 0.32f * (1.0f - attackProgress);
        const float ringLine = std::max(1.0f, line * 0.90f);

        projectedDebug.appendProjectedRing(
            growlSource + forward * std::max(0.01f, worldCellSize * 0.05f),
            baseRadius * (0.80f + attackProgress * 0.65f),
            1.00f, 0.60f, 1.00f, ringAlpha * 0.95f, ringLine, 14);
        projectedDebug.appendProjectedRing(
            growlSource + forward * std::max(0.03f, worldCellSize * 0.14f),
            baseRadius * (1.00f + attackProgress * 0.78f),
            1.00f, 0.70f, 0.82f, ringAlpha * 0.90f, ringLine, 16);
        projectedDebug.appendProjectedRing(
            growlSource + forward * std::max(0.05f, worldCellSize * 0.24f),
            baseRadius * (1.24f + attackProgress * 0.92f),
            0.96f, 0.56f, 0.96f, ringAlpha * 0.82f, ringLine, 16);

        constexpr int kGrowlSpokes = 16;
        for (int s = 0; s < kGrowlSpokes; ++s) {
            const float t =
                (static_cast<float>(s) / static_cast<float>(kGrowlSpokes)) * 6.2831853f;
            const float ripple =
                std::sin(attackProgress * 14.0f + static_cast<float>(s) * 0.65f);
            const glm::vec3 dir = glm::normalize(
                right * (std::cos(t) * 1.10f) +
                up * (std::sin(t) * 1.10f) +
                forward * (1.00f + ripple * 0.12f));
            projectedDebug.appendProjectedLine(
                growlSource,
                growlSource + dir * (baseRadius * (1.65f + attackProgress * 0.90f)),
                1.00f,
                0.64f,
                0.88f,
                ringAlpha * (0.55f + 0.30f * (0.5f + 0.5f * ripple)),
                std::max(1.0f, line * 0.76f));
        }
    }

    if (!args.useLegacyParticleVfxSnapshotBridge &&
        unit.pendingProjectileActive && unit.pendingProjectileTargetId >= 0) {
        if (const PokemonInstance* target = gameWorld->findUnitById(unit.pendingProjectileTargetId)) {
            const glm::vec3 from =
                proxyCenter + glm::vec3(0.0f, std::max(0.10f, extents.height * 0.42f), 0.0f);
            const glm::vec3 to =
                target->position +
                glm::vec3(
                    0.0f,
                    std::max(0.12f, worldCellSize * 0.24f) + target->visualYOffset,
                    0.0f);
            const float spawnT = std::max(0.0f, unit.pendingProjectileSpawnTimeSec);
            const float travelSec = std::max(0.001f, unit.pendingProjectileTravelSec);
            float travelT = 0.0f;
            if (unit.animTimeSec >= spawnT) {
                travelT = std::clamp((unit.animTimeSec - spawnT) / travelSec, 0.0f, 1.0f);
            }
            if (unit.pendingProjectileSpawned && travelT <= 0.0f) {
                travelT = 1.0f;
            }
            if (travelT > 0.0f) {
                const float prevT = std::clamp(travelT - 0.07f, 0.0f, 1.0f);
                const glm::vec3 curPos =
                    glm::mix(from, to, travelT) +
                    glm::vec3(0.0f, std::sin(travelT * 3.1415926f) * 0.08f, 0.0f);
                const glm::vec3 prevPos =
                    glm::mix(from, to, prevT) +
                    glm::vec3(0.0f, std::sin(prevT * 3.1415926f) * 0.08f, 0.0f);
                projectedDebug.appendProjectedLine(
                    prevPos,
                    curPos,
                    0.38f,
                    0.92f,
                    0.34f,
                    0.95f,
                    std::max(1.0f, line * 1.20f));
                projectedDebug.appendProjectedBurst(
                    curPos,
                    forward,
                    std::max(0.02f, worldCellSize * 0.05f),
                    0.52f,
                    0.98f,
                    0.40f,
                    0.85f,
                    std::max(1.0f, line * 0.9f),
                    5);
            }
        }
    }

    const bool pendingImpactBurst =
        (unit.pendingImpactActive && !unit.pendingImpactApplied) ||
        (unit.pendingDamageActive && !unit.pendingDamageApplied);
    const bool allowProjectedImpactFallback =
        !args.useLegacyParticleVfxSnapshotBridge || (pendingGrowl && !args.useLegacyGrowlWaveVfx);
    if (allowProjectedImpactFallback && pendingImpactBurst) {
        const int impactTargetId =
            unit.pendingImpactActive ? unit.pendingImpactTargetId : unit.pendingDamageTargetId;
        const float impactTimeSec =
            unit.pendingImpactActive ? unit.pendingImpactTimeSec : unit.pendingDamageHitTimeSec;
        if (impactTargetId >= 0 && impactTimeSec >= 0.0f) {
            if (const PokemonInstance* target = gameWorld->findUnitById(impactTargetId)) {
                const float distToHit = std::abs(unit.animTimeSec - impactTimeSec);
                if (distToHit <= 0.16f || (unit.pendingDamageApplied || unit.pendingImpactApplied)) {
                    const float burst = std::clamp(1.0f - (distToHit / 0.16f), 0.0f, 1.0f);
                    const float radius =
                        std::max(0.03f, worldCellSize * (0.11f + (1.0f - burst) * 0.11f));
                    const glm::vec3 center =
                        target->position +
                        glm::vec3(
                            0.0f,
                            std::max(0.12f, worldCellSize * 0.24f) + target->visualYOffset,
                            0.0f);
                    const float ia = 0.44f + burst * 0.42f;
                    if (pendingGrowl) {
                        if (!args.useLegacyGrowlWaveVfx) {
                            const glm::vec3 growlSource =
                                proxyCenter +
                                up * std::max(0.10f, extents.height * 0.32f) +
                                forward * std::max(0.03f, extents.halfDepth * 0.56f);
                            projectedDebug.appendProjectedRing(
                                growlSource,
                                radius * 0.98f,
                                1.00f,
                                0.60f,
                                1.00f,
                                ia * 0.95f,
                                std::max(1.0f, line * 0.88f),
                                12);
                            projectedDebug.appendProjectedRing(
                                growlSource + forward * std::max(0.03f, worldCellSize * 0.14f),
                                radius * 1.26f,
                                1.00f,
                                0.70f,
                                0.82f,
                                ia * 0.86f,
                                std::max(1.0f, line * 0.92f),
                                14);
                            projectedDebug.appendProjectedRing(
                                growlSource + forward * std::max(0.05f, worldCellSize * 0.24f),
                                radius * 1.52f,
                                0.96f,
                                0.56f,
                                0.96f,
                                ia * 0.74f,
                                std::max(1.0f, line * 0.86f),
                                14);
                        }
                    } else if (pendingClaw) {
                        projectedDebug.appendProjectedBurst(
                            center,
                            forward,
                            radius * 1.10f,
                            0.95f,
                            (routeMoveLower == "metal_claw") ? 0.95f : 0.82f,
                            (routeMoveLower == "metal_claw") ? 0.99f : 0.82f,
                            ia,
                            std::max(1.0f, line),
                            6);
                    } else if (pendingAqua) {
                        projectedDebug.appendProjectedRing(
                            center,
                            radius * 1.30f,
                            0.36f,
                            0.78f,
                            1.00f,
                            ia,
                            std::max(1.0f, line * 0.95f),
                            14);
                        projectedDebug.appendProjectedBurst(
                            center,
                            up,
                            radius * 0.75f,
                            0.52f,
                            0.90f,
                            1.00f,
                            ia * 0.86f,
                            std::max(1.0f, line * 0.9f),
                            7);
                    } else if (pendingGrass || unit.pendingImpactIsGrass || unit.pendingImpactIsLeechSeed) {
                        projectedDebug.appendProjectedRing(
                            center,
                            radius * 1.18f,
                            0.42f,
                            0.92f,
                            0.34f,
                            ia,
                            std::max(1.0f, line * 0.95f),
                            13);
                        projectedDebug.appendProjectedBurst(
                            center,
                            forward,
                            radius * 0.85f,
                            0.42f,
                            0.96f,
                            0.36f,
                            ia * 0.92f,
                            std::max(1.0f, line * 0.95f),
                            8);
                    } else {
                        projectedDebug.appendProjectedRing(
                            center,
                            radius * 1.15f,
                            1.00f,
                            0.76f,
                            0.28f,
                            ia,
                            std::max(1.0f, line * 0.95f),
                            12);
                        projectedDebug.appendProjectedBurst(
                            center,
                            forward,
                            radius * 0.95f,
                            0.98f,
                            0.72f,
                            0.26f,
                            ia * 0.9f,
                            std::max(1.0f, line),
                            8);
                    }
                }
            }
        }
    }

    const float hudCellPxBase = std::clamp(minDim * 0.070f, 38.0f, 58.0f);
    const float hudCellPx =
        std::clamp(cellPx, hudCellPxBase * 0.90f, hudCellPxBase * 1.10f);
    if (unit.alive) {
        runtime::shared_unit_hud::appendLegacyUnitHud(
            worldQuads,
            lines,
            textLines,
            sharedUnitHudCfg,
            unit,
            cx,
            cy,
            hudCellPx);
    }
}

} // namespace game::runtime::shared_projected_unit_overlays



