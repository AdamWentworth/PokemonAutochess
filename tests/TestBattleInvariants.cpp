// tests/TestBattleInvariants.cpp
#include <string>
#include <optional>

#include "game/GameWorld.h"
#include "game/scripting/ScriptAPI.h"

bool test_battle_invariants(std::string& outFail) {
    GameWorld world;
    ScriptAPI api(&world, nullptr, nullptr, nullptr);

    PokemonInstance attacker;
    attacker.id = 1;
    attacker.name = "attacker";
    attacker.hp = 100;
    attacker.maxHP = 100;
    attacker.alive = true;
    attacker.attackDurationSec = 0.0f; // disable attack-anim gating
    attacker.animAttack1Index = -1;

    PokemonInstance target;
    target.id = 2;
    target.name = "target";
    target.hp = 10;
    target.maxHP = 10;
    target.alive = true;
    target.animAttack1Index = 7;
    target.currentAttackAnimIndex = 42;
    target.attackTimerSec = 5.0f;
    target.attackAnimSpeed = 2.0f;
    target.pendingAttackAfterLanding = true;
    target.queuedAttackDurationSec = 3.0f;
    target.queuedAttackAnimIndex = 99;
    target.chainedFastMove = "tackle";
    target.fastChainTimerSec = 1.0f;

    auto& list = world.getPokemons();
    list.push_back(attacker);
    list.push_back(target);

    int hp = api.applyDamage(1, 2, 3, std::nullopt, std::nullopt, std::nullopt);
    PokemonInstance* t = world.findUnitById(2);
    if (!t) {
        outFail = "Target not found after damage";
        return false;
    }
    if (hp != 7 || t->hp != 7 || !t->alive) {
        outFail = "Expected hp=7 and alive after non-lethal damage";
        return false;
    }

    hp = api.applyDamage(1, 2, 50, std::nullopt, std::nullopt, std::nullopt);
    if (hp != 0 || t->hp != 0 || t->alive) {
        outFail = "Expected hp=0 and alive=false after lethal damage";
        return false;
    }
    if (t->attackTimerSec != 0.0f) {
        outFail = "attackTimerSec should reset to 0 on death";
        return false;
    }
    if (t->attackAnimSpeed != 1.0f) {
        outFail = "attackAnimSpeed should reset to 1.0 on death";
        return false;
    }
    if (t->currentAttackAnimIndex != t->animAttack1Index) {
        outFail = "currentAttackAnimIndex should reset to animAttack1Index on death";
        return false;
    }
    if (t->pendingAttackAfterLanding) {
        outFail = "pendingAttackAfterLanding should reset on death";
        return false;
    }
    if (t->queuedAttackDurationSec != 0.0f || t->queuedAttackAnimIndex != -1) {
        outFail = "queued attack fields should reset on death";
        return false;
    }
    if (!t->chainedFastMove.empty() || t->fastChainTimerSec != 0.0f) {
        outFail = "fast-chain fields should reset on death";
        return false;
    }

    return true;
}
