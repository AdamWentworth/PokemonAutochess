// tests/TestBattleInvariants.cpp
#include <string>
#include <optional>

#include "engine/core/IAssetStore.h"
#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"
#include "game/GameServices.h"
#include "game/GameWorld.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/scripting/ScriptAPI.h"

namespace {
struct NullAssetStore : engine::IAssetStore {
    bool readText(const std::string&, std::string& outText, std::string* outError = nullptr) const override {
        outText.clear();
        if (outError) *outError = "NullAssetStore";
        return false;
    }
    bool readBytes(const std::string&, std::vector<std::uint8_t>& outBytes, std::string* outError = nullptr) const override {
        outBytes.clear();
        if (outError) *outError = "NullAssetStore";
        return false;
    }
    bool exists(const std::string&) const override { return false; }
};
} // namespace

bool test_battle_invariants(std::string& outFail) {
    GameConfigData config;
    GameDataDb db;
    LogBus::Logger log;
    ScriptEventBus events;
    NullAssetStore assets;
    engine::XorShift32 rng(1u);
    engine::ManualTimeSource time;

    GameServices services(config, db, log, events, assets, rng, time);
    GameWorld world(config);
    ScriptAPI api(&world, nullptr, services);

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

    // XP from enemy faint should be split across alive allied board units.
    list.clear();

    PokemonInstance allyA;
    allyA.id = 11;
    allyA.name = "ally_a";
    allyA.side = PokemonSide::Player;
    allyA.alive = true;
    allyA.level = 10;
    allyA.hp = 100;
    allyA.maxHP = 100;
    allyA.xp = 0;

    PokemonInstance allyB;
    allyB.id = 12;
    allyB.name = "ally_b";
    allyB.side = PokemonSide::Player;
    allyB.alive = true;
    allyB.level = 10;
    allyB.hp = 100;
    allyB.maxHP = 100;
    allyB.xp = 0;

    PokemonInstance enemy;
    enemy.id = 13;
    enemy.name = "enemy";
    enemy.side = PokemonSide::Enemy;
    enemy.alive = true;
    enemy.level = 3;
    enemy.baseExp = 70; // floor(70 * 3 / 7) = 30 total XP to split
    enemy.hp = 1;
    enemy.maxHP = 1;

    list.push_back(allyA);
    list.push_back(allyB);
    list.push_back(enemy);

    PokemonInstance* dead = world.findUnitById(13);
    if (!dead) {
        outFail = "Enemy not found for XP split test";
        return false;
    }
    world.handleUnitFaint(*dead);

    PokemonInstance* a = world.findUnitById(11);
    PokemonInstance* b = world.findUnitById(12);
    if (!a || !b) {
        outFail = "Allies missing after XP split test";
        return false;
    }
    if (a->xp != 15 || b->xp != 15) {
        outFail = "Enemy faint XP should split evenly among surviving allies";
        return false;
    }

    return true;
}
