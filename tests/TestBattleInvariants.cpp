// tests/TestBattleInvariants.cpp
#include <string>
#include <optional>

#include "engine/core/IAssetStore.h"
#include "engine/core/Paths.h"
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

    const std::string movesPath = engine::paths::data("config/moves_config.json");
    if (!db.moves.loadConfig(movesPath, nullptr)) {
        outFail = "Failed to load moves config for ScriptAPI damage timing test";
        return false;
    }
    const std::string attackAnimPath = engine::paths::data("config/attack_anim_config.json");
    if (!db.attackAnims.loadConfig(attackAnimPath, &log)) {
        outFail = "Failed to load attack anim config for ScriptAPI damage timing test";
        return false;
    }

    GameWorld timingWorld(config);
    timingWorld.setData(&db);
    timingWorld.setLogger(&log);
    ScriptAPI timingApi(&timingWorld, nullptr, services);

    PokemonInstance timingAttacker;
    timingAttacker.id = PokemonInstance::getNextUnitID();
    timingAttacker.name = "bulbasaur";
    timingAttacker.side = PokemonSide::Player;
    timingAttacker.hp = 100;
    timingAttacker.maxHP = 100;
    timingAttacker.alive = true;
    timingAttacker.attackDurationSec = 1.0f;
    timingAttacker.animAttack1Index = 0;

    PokemonInstance timingTarget;
    timingTarget.id = PokemonInstance::getNextUnitID();
    timingTarget.name = "squirtle";
    timingTarget.side = PokemonSide::Enemy;
    timingTarget.hp = 100;
    timingTarget.maxHP = 100;
    timingTarget.alive = true;

    const int timingAttackerId = timingAttacker.id;
    const int timingTargetId = timingTarget.id;
    auto& timingUnits = timingWorld.getPokemons();
    timingUnits.push_back(timingAttacker);
    timingUnits.push_back(timingTarget);

    int predictedHp = timingApi.applyDamage(
        timingAttackerId,
        timingTargetId,
        13,
        std::optional<float>(0.8f),
        std::optional<std::string>("tackle"),
        std::optional<std::string>("fast"));
    PokemonInstance* timingA = timingWorld.findUnitById(timingAttackerId);
    PokemonInstance* timingT = timingWorld.findUnitById(timingTargetId);
    if (!timingA || !timingT) {
        outFail = "Timing test units not found after ScriptAPI::applyDamage";
        return false;
    }
    if (predictedHp != 87 || timingT->hp != 100) {
        outFail = "Hit-frame damage should be deferred and only return projected HP";
        return false;
    }
    if (!timingA->pendingDamageActive || timingA->pendingDamageTargetId != timingTargetId ||
        timingA->pendingDamageAmount != 13 || timingA->pendingDamageMoveName != "tackle" ||
        timingA->pendingDamageHitTimeSec <= 0.0f) {
        outFail = "Deferred hit-frame state was not queued as expected";
        return false;
    }

    timingA->attackTimerSec = 0.25f;
    timingA->pendingDamageActive = false;
    timingA->pendingDamageTargetId = -1;
    timingA->pendingDamageAmount = 0;
    const int lockedHp = timingApi.applyDamage(
        timingAttackerId,
        timingTargetId,
        7,
        std::optional<float>(0.8f),
        std::optional<std::string>("tackle"),
        std::optional<std::string>("fast"));
    if (lockedHp != 100 || timingT->hp != 100) {
        outFail = "Mid-cycle lock should prevent new damage requests";
        return false;
    }
    if (timingA->pendingDamageActive || timingA->pendingDamageAmount != 0 || timingA->pendingDamageTargetId != -1) {
        outFail = "Mid-cycle lock should not mutate pending damage state";
        return false;
    }

    GameWorld leechWorld(config);
    leechWorld.setData(&db);
    leechWorld.setLogger(&log);
    ScriptAPI leechApi(&leechWorld, nullptr, services);

    PokemonInstance leechAttacker;
    leechAttacker.id = PokemonInstance::getNextUnitID();
    leechAttacker.name = "bulbasaur";
    leechAttacker.side = PokemonSide::Player;
    leechAttacker.hp = 100;
    leechAttacker.maxHP = 100;
    leechAttacker.alive = true;
    leechAttacker.attackDurationSec = 1.0f;
    leechAttacker.animAttack1Index = 0;
    leechAttacker.position = glm::vec3(0.0f, 0.0f, 0.0f);

    PokemonInstance leechTarget;
    leechTarget.id = PokemonInstance::getNextUnitID();
    leechTarget.name = "squirtle";
    leechTarget.side = PokemonSide::Enemy;
    leechTarget.hp = 100;
    leechTarget.maxHP = 100;
    leechTarget.alive = true;
    leechTarget.position = glm::vec3(2.0f, 0.0f, 0.0f);

    const int leechAttackerId = leechAttacker.id;
    const int leechTargetId = leechTarget.id;
    auto& leechUnits = leechWorld.getPokemons();
    leechUnits.push_back(leechAttacker);
    leechUnits.push_back(leechTarget);

    const int leechPredictedHp = leechApi.applyDamage(
        leechAttackerId,
        leechTargetId,
        11,
        std::optional<float>(0.8f),
        std::optional<std::string>("leech_seed"),
        std::nullopt);
    PokemonInstance* leechA = leechWorld.findUnitById(leechAttackerId);
    PokemonInstance* leechT = leechWorld.findUnitById(leechTargetId);
    if (!leechA || !leechT) {
        outFail = "Leech Seed test units not found after ScriptAPI::applyDamage";
        return false;
    }
    if (leechPredictedHp != 100 || leechT->hp != 100) {
        outFail = "Leech Seed should queue deferred impact without immediate HP damage";
        return false;
    }
    if (!leechA->pendingImpactActive || !leechA->pendingImpactIsLeechSeed ||
        leechA->pendingImpactTargetId != leechTargetId || leechA->pendingImpactTimeSec < 0.0f ||
        !leechA->pendingProjectileActive || leechA->pendingProjectileTargetId != leechTargetId ||
        leechA->pendingProjectileSpawnTimeSec < 0.0f || leechA->pendingProjectileTravelSec < 0.01f) {
        outFail = "Leech Seed should queue pending projectile and impact state";
        return false;
    }
    if (leechA->pendingDamageActive) {
        outFail = "Leech Seed should not queue deferred direct-damage hit-frame state";
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
