#include "game/world/GameWorld.h"

#include <algorithm>
#include <cmath>

#include "game/config/LeechSeedConfigDB.h"

void GameWorld::ensureLeechSeedConfigLoaded()
{
    if (leechSeedConfigLoaded) return;
    leechSeedConfigLoaded = true;

    LeechSeedConfigDB::get().ensureLoaded();
    leechSeedConfig = LeechSeedConfigDB::get().getConfig();
}

void GameWorld::applyLeechSeed(int attackerId, int targetId)
{
    ensureLeechSeedConfigLoaded();

    if (leechSeedConfig.durationSec <= 0.0f) return;

    auto* attacker = findUnitById(attackerId);
    auto* target = findUnitById(targetId);
    if (!attacker || !target) return;
    if (!attacker->alive || !target->alive) return;

    target->leechSeeded = true;
    target->leechSeedSourceId = attackerId;
    target->leechSeedTimeLeftSec = leechSeedConfig.durationSec;
    target->leechSeedTickTimerSec = std::max(0.01f, leechSeedConfig.tickIntervalSec);
}

void GameWorld::updateLeechSeedStatus(float dt)
{
    ensureLeechSeedConfigLoaded();

    if (leechSeedConfig.durationSec <= 0.0f) return;

    dt = std::clamp(dt, 0.0f, 0.1f);

    const float drainSpeed = 3.0f;     // world units per second
    const float minTravel = 0.20f;
    const float maxTravel = 0.60f;

    for (auto& target : pokemons) {
        if (!target.leechSeeded) continue;

        if (!target.alive) {
            target.leechSeeded = false;
            continue;
        }

        auto* source = findUnitById(target.leechSeedSourceId);
        if (!source || !source->alive) {
            target.leechSeeded = false;
            continue;
        }

        target.leechSeedTimeLeftSec = std::max(0.0f, target.leechSeedTimeLeftSec - dt);
        target.leechSeedTickTimerSec -= dt;

        while (target.leechSeedTickTimerSec <= 0.0f) {
            const float pct = std::max(0.0f, leechSeedConfig.sapPercent);
            int sap = static_cast<int>(std::round(static_cast<float>(target.maxHP) * pct));
            sap = std::max(leechSeedConfig.minSap, sap);
            if (sap <= 0) break;

            // Apply sap damage.
            target.hp = std::max(0, target.hp - sap);
            if (target.hp <= 0) {
                handleUnitFaint(target);
            }

            // VFX: drain dots to source.
            {
                const glm::vec3 tpos = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);
                const glm::vec3 spos = source->position + glm::vec3(0.0f, source->visualYOffset, 0.0f);

                const float dist = glm::distance(tpos, spos);
                float travelSec = dist / std::max(0.1f, drainSpeed);
                travelSec = std::clamp(travelSec, minTravel, maxTravel);

                if (legacyModelRenderPathEnabled) {
                    leechSeedDrainVfx.emitBetween(tpos, spos, travelSec);
                }

                const float healMult = std::max(0.0f, leechSeedConfig.healMultiplier);
                int heal = static_cast<int>(std::round(static_cast<float>(sap) * healMult));
                if (heal > 0) {
                    pendingLeechHeals.push_back({ source->id, heal, travelSec });
                }
            }

            target.leechSeedTickTimerSec += std::max(0.01f, leechSeedConfig.tickIntervalSec);

            if (!target.alive) break;
        }

        if (target.leechSeedTimeLeftSec <= 0.0f) {
            target.leechSeeded = false;
        }
    }

    // Apply pending heals when drain dots should arrive.
    if (!pendingLeechHeals.empty()) {
        for (auto& h : pendingLeechHeals) {
            h.timeLeftSec -= dt;
        }

        pendingLeechHeals.erase(
            std::remove_if(pendingLeechHeals.begin(), pendingLeechHeals.end(),
                [&](const PendingLeechHeal& h) {
                    if (h.timeLeftSec > 0.0f) return false;
                    auto* source = findUnitById(h.sourceId);
                    if (source && source->alive && h.amount > 0) {
                        source->hp = std::min(source->maxHP, source->hp + h.amount);
                        if (legacyModelRenderPathEnabled) {
                            const glm::vec3 spos = source->position + glm::vec3(0.0f, source->visualYOffset, 0.0f);
                            healPlusVfx.emitAt(spos);
                        }
                    }
                    return true;
                }),
            pendingLeechHeals.end()
        );
    }
}

