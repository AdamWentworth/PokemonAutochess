#include <algorithm>
#include <cmath>
#include <string>

#include "game/GameConfig.h"
#include "game/GameWorld.h"
#include "game/PokemonInstance.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool nearf(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

PokemonInstance makeUnit(const std::string& name,
                         PokemonSide side,
                         bool alive,
                         int hp = 100,
                         int maxHp = 100) {
    PokemonInstance unit;
    unit.id = PokemonInstance::getNextUnitID();
    unit.name = name;
    unit.side = side;
    unit.alive = alive;
    unit.maxHP = std::max(1, maxHp);
    unit.hp = std::clamp(hp, 0, unit.maxHP);
    unit.position = glm::vec3(0.0f);
    return unit;
}

}  // namespace

bool test_gameworld_leechseed_apply_contract(std::string& outFail) {
    GameConfigData cfg;
    GameWorld world(cfg);
    world.setRenderEnabled(false);

    const PokemonInstance attacker = makeUnit("attacker", PokemonSide::Player, true);
    const PokemonInstance target = makeUnit("target", PokemonSide::Enemy, true);
    const PokemonInstance deadAttacker = makeUnit("dead_attacker", PokemonSide::Player, false);
    const PokemonInstance deadTarget = makeUnit("dead_target", PokemonSide::Enemy, false);

    const int attackerId = attacker.id;
    const int targetId = target.id;
    const int deadAttackerId = deadAttacker.id;
    const int deadTargetId = deadTarget.id;

    world.getPokemons().push_back(attacker);
    world.getPokemons().push_back(target);
    world.getPokemons().push_back(deadAttacker);
    world.getPokemons().push_back(deadTarget);

    PokemonInstance* targetRef = world.findUnitById(targetId);
    PokemonInstance* deadTargetRef = world.findUnitById(deadTargetId);
    if (!expect(targetRef != nullptr && deadTargetRef != nullptr, "Expected target units to be present.", outFail)) return false;

    world.applyLeechSeed(999999, targetId);
    if (!expect(!targetRef->leechSeeded, "Invalid attacker id should not apply leech seed.", outFail)) return false;

    world.applyLeechSeed(attackerId, 999999);
    if (!expect(!targetRef->leechSeeded, "Invalid target id should not apply leech seed.", outFail)) return false;

    world.applyLeechSeed(deadAttackerId, targetId);
    if (!expect(!targetRef->leechSeeded, "Dead attacker should not apply leech seed.", outFail)) return false;

    world.applyLeechSeed(attackerId, deadTargetId);
    if (!expect(!deadTargetRef->leechSeeded, "Dead target should not receive leech seed.", outFail)) return false;

    world.applyLeechSeed(attackerId, targetId);
    if (!expect(targetRef->leechSeeded, "Valid attacker/target should apply leech seed.", outFail)) return false;
    if (!expect(targetRef->leechSeedSourceId == attackerId, "Leech-seed source id mismatch.", outFail)) return false;
    if (!expect(targetRef->leechSeedTimeLeftSec > 0.0f, "Leech-seed duration should be positive.", outFail)) return false;
    if (!expect(targetRef->leechSeedTickTimerSec > 0.0f, "Leech-seed tick timer should be positive.", outFail)) return false;

    return true;
}

bool test_gameworld_leechseed_dt_clamp(std::string& outFail) {
    GameConfigData cfg;
    GameWorld world(cfg);
    world.setRenderEnabled(false);

    const PokemonInstance attacker = makeUnit("attacker", PokemonSide::Player, true);
    const PokemonInstance target = makeUnit("target", PokemonSide::Enemy, true);
    const int attackerId = attacker.id;
    const int targetId = target.id;

    world.getPokemons().push_back(attacker);
    world.getPokemons().push_back(target);

    world.applyLeechSeed(attackerId, targetId);

    PokemonInstance* targetRef = world.findUnitById(targetId);
    if (!expect(targetRef != nullptr && targetRef->leechSeeded,
                "Expected target to be leech-seeded before dt clamp test.", outFail)) return false;

    targetRef->leechSeedTickTimerSec = 99.0f;  // prevent tick processing, isolate dt clamp behavior
    const float beforeTimeLeft = targetRef->leechSeedTimeLeftSec;
    const float beforeTickTimer = targetRef->leechSeedTickTimerSec;

    world.update(1.0f);  // updateLeechSeedStatus clamps dt to 0.1

    const float timeDelta = beforeTimeLeft - targetRef->leechSeedTimeLeftSec;
    const float tickDelta = beforeTickTimer - targetRef->leechSeedTickTimerSec;

    if (!expect(timeDelta >= 0.099f && timeDelta <= 0.101f,
                "Leech-seed duration should decay by clamped dt (0.1).", outFail)) return false;
    if (!expect(tickDelta >= 0.099f && tickDelta <= 0.101f,
                "Leech-seed tick timer should decay by clamped dt (0.1).", outFail)) return false;

    return true;
}

bool test_gameworld_leechseed_clears_for_invalid_state(std::string& outFail) {
    GameConfigData cfg;

    {
        GameWorld world(cfg);
        world.setRenderEnabled(false);

        const PokemonInstance deadSource = makeUnit("dead_source", PokemonSide::Player, false);
        PokemonInstance target = makeUnit("target", PokemonSide::Enemy, true);
        target.leechSeeded = true;
        target.leechSeedSourceId = deadSource.id;
        target.leechSeedTimeLeftSec = 4.0f;
        target.leechSeedTickTimerSec = 1.0f;

        world.getPokemons().push_back(deadSource);
        world.getPokemons().push_back(target);

        const int targetId = target.id;
        world.update(0.05f);

        const PokemonInstance* targetRef = world.findUnitById(targetId);
        if (!expect(targetRef != nullptr, "Expected target to exist for dead-source check.", outFail)) return false;
        if (!expect(!targetRef->leechSeeded, "Leech seed should clear when source is dead.", outFail)) return false;
    }

    {
        GameWorld world(cfg);
        world.setRenderEnabled(false);

        const PokemonInstance source = makeUnit("source", PokemonSide::Player, true);
        PokemonInstance deadTarget = makeUnit("dead_target", PokemonSide::Enemy, false, 0, 100);
        deadTarget.leechSeeded = true;
        deadTarget.leechSeedSourceId = source.id;
        deadTarget.leechSeedTimeLeftSec = 4.0f;
        deadTarget.leechSeedTickTimerSec = 1.0f;

        world.getPokemons().push_back(source);
        world.getPokemons().push_back(deadTarget);

        const int deadTargetId = deadTarget.id;
        world.update(0.05f);

        const PokemonInstance* deadTargetRef = world.findUnitById(deadTargetId);
        if (!expect(deadTargetRef != nullptr, "Expected dead target to exist for invalid-target check.", outFail)) return false;
        if (!expect(!deadTargetRef->leechSeeded, "Leech seed should clear when target is dead.", outFail)) return false;
        if (!expect(nearf(deadTargetRef->leechSeedTimeLeftSec, 4.0f),
                    "Dead-target clear should happen before duration decay.", outFail)) return false;
    }

    return true;
}
