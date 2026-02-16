#include "game/world/GameWorld.h"

#include <algorithm>

#include "game/GameConfig.h"

#include "engine/render/Model.h"

void GameWorld::beginFaint(PokemonInstance& target) {
    if (target.fainting) return;

    target.fainting = true;
    target.faintTimerSec = 0.0f;
    target.fadeOutSec = std::max(0.0f, config.faintFadeSec);
    target.fadeOutTimerSec = target.fadeOutSec;
    target.visualScale = 1.0f;

    if (target.animFaintIndex >= 0) {
        target.activeAnimIndex = target.animFaintIndex;
        target.currentAttackAnimIndex = target.animFaintIndex;
        target.animTimeSec = 0.0f;
        target.attackAnimSpeed = 1.0f;
        if (target.faintAnimDurationSec <= 0.0f && target.model) {
            target.faintAnimDurationSec = target.model->getAnimationDurationSec(target.animFaintIndex);
        }
    } else {
        target.faintAnimDurationSec = 0.0f;
    }
}

void GameWorld::handleUnitFaint(PokemonInstance& target) {
    if (!target.alive) return;

    target.hp = 0;
    target.alive = false;

    target.isMoving = false;
    target.attackTimerSec = 0.0f;
    target.attackAnimSpeed = 1.0f;
    target.currentAttackAnimIndex = target.animAttack1Index;
    target.pendingAttackAfterLanding = false;
    target.queuedAttackDurationSec = 0.0f;
    target.queuedAttackAnimIndex = -1;
    target.chainedFastMove.clear();
    target.fastChainTimerSec = 0.0f;
    target.pendingDamageActive = false;
    target.pendingDamageApplied = false;
    target.pendingDamageTargetId = -1;
    target.pendingDamageAmount = 0;
    target.pendingDamageHitTimeSec = 0.0f;
    target.pendingDamageMoveName.clear();
    target.animIndexCache.clear();

    target.leechSeeded = false;
    target.leechSeedSourceId = -1;
    target.leechSeedTimeLeftSec = 0.0f;
    target.leechSeedTickTimerSec = 0.0f;

    beginFaint(target);
    awardXpForFaint(target);
}

void GameWorld::healPlayerUnitsToFull() {
    auto healList = [&](std::vector<PokemonInstance>& list) {
        for (auto& u : list) {
            if (u.side != PokemonSide::Player) continue;

            // Between rounds, allied units are restored and ready again.
            u.alive = true;
            u.fainting = false;
            u.faintTimerSec = 0.0f;
            u.fadeOutTimerSec = 0.0f;
            u.visualScale = 1.0f;
            u.captureInProgress = false;
            u.captureScale = 1.0f;
            u.captureTintStrength = 0.0f;

            u.isMoving = false;
            u.moveT = 1.0f;
            u.attackTimerSec = 0.0f;
            u.pendingDamageActive = false;
            u.pendingDamageApplied = false;
            u.pendingDamageMoveName.clear();
            u.pendingProjectileActive = false;
            u.pendingProjectileSpawned = false;
            u.pendingImpactActive = false;
            u.pendingImpactApplied = false;

            u.leechSeeded = false;
            u.leechSeedSourceId = -1;
            u.leechSeedTimeLeftSec = 0.0f;
            u.leechSeedTickTimerSec = 0.0f;

            u.hp = u.maxHP;
        }
    };

    healList(pokemons);
    healList(benchPokemons);
}

void GameWorld::capturePlayerPositionsForBattle() {
    battleStartPositions.clear();
    auto capture = [&](const std::vector<PokemonInstance>& list) {
        for (const auto& u : list) {
            if (u.side != PokemonSide::Player) continue;
            battleStartPositions[u.id] = u.position;
        }
    };
    capture(pokemons);
    capture(benchPokemons);
}

void GameWorld::restorePlayerPositionsAfterBattle() {
    auto restore = [&](std::vector<PokemonInstance>& list) {
        for (auto& u : list) {
            if (u.side != PokemonSide::Player) continue;
            auto it = battleStartPositions.find(u.id);
            if (it == battleStartPositions.end()) continue;
            u.position = it->second;
            u.rotation.y = 180.0f;
            u.isMoving = false;
            u.moveT = 1.0f;
            u.committedDest = {-1, -1};
            u.moveFrom = u.position;
            u.moveTo = u.position;
        }
    };
    restore(pokemons);
    restore(benchPokemons);
    battleStartPositions.clear();
}

void GameWorld::updateFaint(PokemonInstance& target, float dt) {
    if (!target.fainting || !target.model) return;

    target.faintTimerSec += dt;

    const float dur = std::max(0.0f, target.faintAnimDurationSec);
    if (target.animFaintIndex >= 0) {
        const float clipDur = target.model->getAnimationDurationSec(target.animFaintIndex);
        const float clampDur = (clipDur > 0.0f) ? clipDur : dur;
        if (clampDur > 0.0f) {
            target.animTimeSec = std::min(target.animTimeSec + dt, clampDur - 0.0001f);
        } else {
            target.animTimeSec += dt;
        }
    }

    const bool animDone = (dur <= 0.0f) || (target.faintTimerSec >= dur);
    if (!animDone) return;

    if (target.fadeOutSec <= 0.0f) {
        target.visualScale = 0.0f;
        target.fainting = false;
        return;
    }

    target.fadeOutTimerSec = std::max(0.0f, target.fadeOutTimerSec - dt);
    const float t = 1.0f - (target.fadeOutTimerSec / target.fadeOutSec);
    target.visualScale = std::clamp(1.0f - t, 0.0f, 1.0f);

    if (target.fadeOutTimerSec <= 0.0f) {
        target.visualScale = 0.0f;
        target.fainting = false;
    }
}

