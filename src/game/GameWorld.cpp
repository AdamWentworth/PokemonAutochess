// src/game/GameWorld.cpp
#include <cmath>
#include <limits>
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <cctype>
#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "engine/core/Random.h"

namespace {
std::string Capitalize(std::string s) {
    if (s.empty()) return s;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

#include "GameWorld.h"
#include "GameConfig.h"

#include "engine/utils/ResourceManager.h"
#include "engine/render/Model.h"
#include "engine/render/Camera3D.h"
#include "engine/render/BoardRenderer.h"

#include "animation/FlightLocomotion.h"

#include "config/GameDataDb.h"
#include "config/PokemonConfigLoader.h"
#include "config/MovesConfigLoader.h"
#include "config/AnimSetLoader.h"

#include "logging/LoggerUtil.h"
#include "logging/DebugTrace.h"

#include "ui/HealthBarQuery.h"

namespace {
glm::vec3 SafeForwardXZ(const glm::vec3& v) {
    glm::vec3 f(v.x, 0.0f, v.z);
    const float len = glm::length(f);
    if (len <= 0.0001f) return glm::vec3(0.0f, 0.0f, 1.0f);
    return f / len;
}

glm::mat4 BuildModelInstanceTransform(const PokemonInstance& instance) {
    const float scaleFactor = (instance.model ? instance.model->getScaleFactor() : 1.0f) *
                              std::max(0.0f, instance.visualScale) *
                              std::max(0.0f, instance.captureScale);

    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
    const glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
    const glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
    const glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
    const glm::vec3 renderPos = instance.position + glm::vec3(0.0f, instance.visualYOffset, 0.0f);
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), renderPos);

    return translation * rotationY * rotationX * rotationZ * scale;
}

template <size_t N>
bool TryResolveAnimatedNodeWorld(const PokemonInstance& unit,
                                 const std::array<const char*, N>& nodeNames,
                                 glm::vec3& outWorldPos) {
    if (!unit.model) return false;

    const glm::mat4 instanceM = BuildModelInstanceTransform(unit);
    const int activeAnim = (unit.activeAnimIndex >= 0) ? unit.activeAnimIndex : unit.animIdleIndex;
    const int idleAnim = unit.animIdleIndex;

    auto tryAnim = [&](int animIndex) -> bool {
        if (animIndex < 0) return false;
        for (const char* nodeName : nodeNames) {
            if (!nodeName || !nodeName[0]) continue;
            glm::mat4 nodeGlobal(1.0f);
            if (!unit.model->getNodeGlobalTransformByName(unit.animTimeSec, animIndex, nodeName, nodeGlobal)) continue;
            const glm::mat4 nodeWorld = instanceM * nodeGlobal;
            outWorldPos = glm::vec3(nodeWorld[3]);
            return true;
        }
        return false;
    };

    if (tryAnim(activeAnim)) return true;
    if (idleAnim != activeAnim && tryAnim(idleAnim)) return true;
    return false;
}
} // namespace

GameWorld::GameWorld(const GameConfigData& cfg)
    : config(cfg) {
    money = std::max(0, config.startingCash);
}

void GameWorld::applyLevelScaling(PokemonInstance& inst, int level, bool preserveHp) const {
    const auto& cfg = config;
    const int useLevel = (level <= 0) ? cfg.baseLevel : level;

    inst.level = useLevel;

    float hpRatio = 1.0f;
    if (preserveHp && inst.maxHP > 0) {
        hpRatio = std::clamp(static_cast<float>(inst.hp) / static_cast<float>(inst.maxHP), 0.0f, 1.0f);
    }

    const float hpMult  = std::pow(1.0f + cfg.perLevelHpBoost, static_cast<float>(useLevel - 1));
    const float atkMult = std::pow(1.0f + cfg.perLevelAttackBoost, static_cast<float>(useLevel - 1));
    const float spdMult = std::pow(1.0f + cfg.perLevelSpeedBoost, static_cast<float>(useLevel - 1));

    inst.maxHP         = static_cast<int>(std::round(static_cast<float>(inst.baseHp) * hpMult));
    if (preserveHp) {
        inst.hp = std::clamp(static_cast<int>(std::round(static_cast<float>(inst.maxHP) * hpRatio)), 1, inst.maxHP);
    } else {
        inst.hp = inst.maxHP;
    }
    inst.attack        = static_cast<int>(std::round(static_cast<float>(inst.baseAttack) * atkMult));
    inst.movementSpeed = inst.baseMovementSpeed * spdMult;
}

static const LoadoutEntry* pickLoadoutForLevel(const PokemonStats& ps, int level) {
    const LoadoutEntry* best = nullptr;

    for (const auto& [lvl, le] : ps.loadoutByLevel) {
        if (lvl <= level) best = &le;
        else break;
    }

    return best;
}

void GameWorld::applyLoadoutForLevel(PokemonInstance& inst, bool preserveEnergy) const {
    if (!data) {
        inst.fastMove.clear();
        inst.chargedMove.clear();
        inst.maxEnergy = 100;
        if (!preserveEnergy) inst.energy = 0;
        return;
    }

    const PokemonStats* ps = data->pokemon.getStats(inst.name);
    if (!ps) {
        inst.fastMove.clear();
        inst.chargedMove.clear();
        inst.maxEnergy = 100;
        if (!preserveEnergy) inst.energy = 0;
        return;
    }

    const LoadoutEntry* le = pickLoadoutForLevel(*ps, inst.level);
    if (le) {
        inst.fastMove = le->fast;
        inst.chargedMove = le->hasCharged ? le->charged : std::string();
    } else {
        inst.fastMove.clear();
        inst.chargedMove.clear();
    }

    inst.maxEnergy = 100;
    if (!inst.chargedMove.empty()) {
        if (const auto* md = data->moves.getMove(inst.chargedMove)) {
            if (md->energyCost > 0) inst.maxEnergy = md->energyCost;
        }
    }
    if (preserveEnergy) {
        inst.energy = std::min(inst.energy, inst.maxEnergy);
    } else {
        inst.energy = 0;
    }
}

int GameWorld::xpToNextLevel(int level) const {
    const auto& cfg = config;
    if (cfg.xpLevelBase <= 0) return std::numeric_limits<int>::max();

    const int useLevel = std::max(1, level);
    const float growth = (cfg.xpLevelGrowth > 0.0f) ? cfg.xpLevelGrowth : 1.0f;
    const float raw = static_cast<float>(cfg.xpLevelBase) * std::pow(growth, static_cast<float>(useLevel - 1));
    return std::max(1, static_cast<int>(std::round(raw)));
}

int GameWorld::totalXpFromLevelProgress(const PokemonInstance& unit) const {
    int total = 0;
    const int lvl = std::max(1, unit.level);
    for (int l = 1; l < lvl; ++l) {
        total += xpToNextLevel(l);
    }
    total += std::max(0, unit.xp);
    return std::max(0, total);
}

void GameWorld::levelProgressFromTotalXp(int totalXp, int& outLevel, int& outXp) const {
    int remaining = std::max(0, totalXp);
    int lvl = 1;
    const int maxLevel = config.xpMaxLevel;

    while (true) {
        if (maxLevel > 0 && lvl >= maxLevel) {
            outLevel = maxLevel;
            outXp = std::min(remaining, xpToNextLevel(maxLevel));
            return;
        }
        const int need = xpToNextLevel(lvl);
        if (remaining < need) {
            outLevel = lvl;
            outXp = remaining;
            return;
        }
        remaining -= need;
        ++lvl;
    }
}

bool GameWorld::mergeOneTripleForPlayer() {
    struct UnitRef {
        int id = -1;
        bool onBoard = false;
        int totalXp = 0;
    };

    std::unordered_map<std::string, std::vector<UnitRef>> bySpecies;
    bySpecies.reserve(pokemons.size() + benchPokemons.size());

    for (const auto& u : pokemons) {
        if (u.side != PokemonSide::Player) continue;
        if (!u.alive) continue;
        if (u.captureInProgress) continue;
        bySpecies[u.name].push_back(UnitRef{u.id, true, totalXpFromLevelProgress(u)});
    }
    for (const auto& u : benchPokemons) {
        if (u.side != PokemonSide::Player) continue;
        if (!u.alive) continue;
        if (u.captureInProgress) continue;
        bySpecies[u.name].push_back(UnitRef{u.id, false, totalXpFromLevelProgress(u)});
    }

    std::string species;
    std::vector<UnitRef> refs;
    for (auto& kv : bySpecies) {
        if (kv.second.size() < 3) continue;
        std::sort(kv.second.begin(), kv.second.end(), [](const UnitRef& a, const UnitRef& b) {
            if (a.totalXp != b.totalXp) return a.totalXp > b.totalXp;
            if (a.onBoard != b.onBoard) return a.onBoard;
            return a.id < b.id;
        });
        species = kv.first;
        refs = {kv.second[0], kv.second[1], kv.second[2]};
        break;
    }

    if (refs.size() < 3) return false;

    const int keeperId = refs[0].id;
    const int removeA = refs[1].id;
    const int removeB = refs[2].id;
    const int combinedTotalXp = refs[0].totalXp + refs[1].totalXp + refs[2].totalXp;

    auto eraseById = [&](std::vector<PokemonInstance>& list, int id) {
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](const PokemonInstance& u) { return u.id == id; }),
                   list.end());
    };

    eraseById(pokemons, removeA);
    eraseById(pokemons, removeB);
    eraseById(benchPokemons, removeA);
    eraseById(benchPokemons, removeB);
    battleStartPositions.erase(removeA);
    battleStartPositions.erase(removeB);

    PokemonInstance* keeper = findUnitById(keeperId);
    if (!keeper) return false;

    int newLevel = 1;
    int newXp = 0;
    levelProgressFromTotalXp(combinedTotalXp, newLevel, newXp);
    keeper->xp = std::max(0, newXp);
    applyLevelScaling(*keeper, newLevel, /*preserveHp=*/true);
    applyLoadoutForLevel(*keeper, /*preserveEnergy=*/true);

    if (log) {
        game::log::info(log, "Merged 3x " + Capitalize(species) + " -> Lv" + std::to_string(keeper->level));
    }
    return true;
}

void GameWorld::mergeTriplesForPlayer() {
    // Re-run until no 3-of-a-kind groups remain.
    while (mergeOneTripleForPlayer()) {}
}

int GameWorld::xpFromFaint(const PokemonInstance& dead) const {
    if (dead.baseExp > 0 && dead.level > 0) {
        const float mult = (config.xpYieldMult > 0.0f) ? config.xpYieldMult : 1.0f;
        const float raw = (static_cast<float>(dead.baseExp) * static_cast<float>(dead.level) * mult) / 7.0f;
        const int xp = static_cast<int>(std::floor(raw));
        return std::max(1, xp);
    }
    return std::max(0, config.xpPerFaint);
}

void GameWorld::addXp(PokemonInstance& unit, int amount) {
    if (amount <= 0) return;
    unit.xp = std::max(0, unit.xp + amount);

    const int maxLevel = config.xpMaxLevel;
    while (unit.xp >= xpToNextLevel(unit.level)) {
        if (maxLevel > 0 && unit.level >= maxLevel) {
            unit.xp = std::min(unit.xp, xpToNextLevel(unit.level));
            break;
        }

        unit.xp -= xpToNextLevel(unit.level);

        const int nextLevel = unit.level + 1;
        applyLevelScaling(unit, nextLevel, /*preserveHp=*/true);
        applyLoadoutForLevel(unit, /*preserveEnergy=*/true);
    }
}

void GameWorld::awardXpForFaint(const PokemonInstance& dead) {
    if (dead.side != PokemonSide::Enemy) return;

    const int totalXp = xpFromFaint(dead);
    if (totalXp <= 0) return;

    std::vector<PokemonInstance*> recipients;
    recipients.reserve(pokemons.size());
    for (auto& u : pokemons) {
        if (!u.alive) continue;
        if (u.side != PokemonSide::Player) continue;
        recipients.push_back(&u);
    }
    if (recipients.empty()) return;

    std::sort(recipients.begin(), recipients.end(),
              [](const PokemonInstance* a, const PokemonInstance* b) {
                  return a->id < b->id;
              });

    const int split = totalXp / static_cast<int>(recipients.size());
    int rem = totalXp % static_cast<int>(recipients.size());
    for (auto* u : recipients) {
        int grant = split;
        if (rem > 0) {
            grant += 1;
            --rem;
        }
        addXp(*u, grant);
    }
}

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

void GameWorld::resetForNewGame(int startingMoney) {
    pokemons.clear();
    benchPokemons.clear();
    battleStartPositions.clear();
    captureAttempts.clear();
    pendingLeechHeals.clear();
    classicShopCards.clear();

    selectedItemId.clear();
    items.clear();
    const int resetMoney = (startingMoney >= 0) ? startingMoney : config.startingCash;
    money = std::max(0, resetMoney);

    classicWinStreak = 0;
    classicLossStreak = 0;
    classicRoundsCompleted = 0;

    boardInteractionLocked = false;
    unitDragActive = false;
    uiClickBlockFrames = 0;
    resetCombatBalance();

    sharedLoopAnimTimeSec = 0.0f;
}

GameWorld::ClassicRoundIncomeResult GameWorld::awardClassicRoundIncome(bool playerWon) {
    ClassicRoundIncomeResult result{};
    result.won = playerWon;

    if (playerWon) {
        classicWinStreak = std::max(0, classicWinStreak + 1);
        classicLossStreak = 0;
    } else {
        classicLossStreak = std::max(0, classicLossStreak + 1);
        classicWinStreak = 0;
    }

    const int activeStreak = std::max(classicWinStreak, classicLossStreak);

    result.baseIncome = std::max(0, config.classicBaseIncome);
    const int per10 = std::max(0, config.classicInterestPer10);
    const int interestCap = std::max(0, config.classicInterestCap);
    const int interest = (std::max(0, money) / 10) * per10;
    result.interestIncome = std::min(interestCap, interest);
    if (activeStreak >= 6) {
        result.streakIncome = std::max(0, config.classicStreakBonus6Plus);
    } else if (activeStreak >= 4) {
        result.streakIncome = std::max(0, config.classicStreakBonus4To5);
    } else if (activeStreak >= 2) {
        result.streakIncome = std::max(0, config.classicStreakBonus2To3);
    } else {
        result.streakIncome = 0;
    }

    result.totalIncome = std::max(0, result.baseIncome + result.interestIncome + result.streakIncome);
    addMoney(result.totalIncome);

    classicRoundsCompleted += 1;
    result.roundIndex = classicRoundsCompleted;
    result.winStreak = classicWinStreak;
    result.lossStreak = classicLossStreak;
    return result;
}

void GameWorld::addMoney(int amount) {
    if (amount <= 0) return;
    money = std::max(0, money + amount);
}

bool GameWorld::spendMoney(int amount) {
    if (amount <= 0) return true;
    if (money < amount) return false;
    money -= amount;
    return true;
}

int GameWorld::getSellValueForSpecies(const std::string& pokemonName) const {
    if (!data) return 1;
    const PokemonStats* stats = data->pokemon.getStats(pokemonName);
    if (!stats) return 1;
    return std::max(1, stats->shopBaseCost);
}

void GameWorld::setClassicShopCards(const std::vector<ClassicShopCard>& cards) {
    classicShopCards.clear();
    classicShopCards.reserve(cards.size());
    for (const auto& c : cards) {
        if (c.name.empty()) continue;
        ClassicShopCard out = c;
        out.level = std::max(1, out.level);
        out.cost = std::max(0, out.cost);
        classicShopCards.push_back(std::move(out));
    }
}

void GameWorld::clearClassicShopCards() {
    classicShopCards.clear();
}

int GameWorld::getItemCount(const std::string& item) const {
    auto it = items.find(item);
    if (it == items.end()) return 0;
    return std::max(0, it->second);
}

void GameWorld::addItem(const std::string& item, int amount) {
    if (item.empty() || amount <= 0) return;
    items[item] = std::max(0, items[item] + amount);
}

bool GameWorld::consumeItem(const std::string& item, int amount) {
    if (item.empty() || amount <= 0) return true;
    auto it = items.find(item);
    if (it == items.end() || it->second < amount) return false;
    it->second -= amount;
    return true;
}

std::vector<std::pair<std::string, int>> GameWorld::listItems() const {
    std::vector<std::pair<std::string, int>> out;
    out.reserve(items.size());
    for (const auto& kv : items) {
        if (kv.second <= 0) continue;
        out.emplace_back(kv.first, kv.second);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    return out;
}

void GameWorld::setSelectedItem(const std::string& itemId) {
    selectedItemId = itemId;
}

void GameWorld::clearSelectedItem() {
    selectedItemId.clear();
}

bool GameWorld::tryUseHealingItem(const std::string& itemId, int targetId) {
    auto* target = findUnitById(targetId);
    if (!target) return false;
    if (target->side != PokemonSide::Player) return false;
    if (!target->alive) return false;

    if (!consumeItem(itemId, 1)) return false;

    const float pct = std::max(0.0f, config.potionHealPct);
    const int flat = std::max(0, config.potionHealFlat);
    const int healAmount = std::max(1, static_cast<int>(std::round(static_cast<float>(target->maxHP) * pct)) + flat);
    target->hp = std::min(target->maxHP, target->hp + healAmount);

    if (log) {
        game::log::info(log, "Used " + itemId + " on " + Capitalize(target->name) +
            " (+" + std::to_string(healAmount) + " HP)");
    }
    return true;
}

bool GameWorld::startCaptureAttempt(int targetId, float ballMult, const glm::vec3* throwOrigin) {
    auto* target = findUnitById(targetId);
    if (!target) return false;
    if (target->side != PokemonSide::Enemy) return false;
    if (target->captureInProgress) return false;
    if (!target->alive && !target->fainting) return false; // already gone

    const PokemonStats* stats = data ? data->pokemon.getStats(target->name) : nullptr;
    const float baseRate = stats ? stats->catchRate : 0.0f;
    if (baseRate <= 0.0f) return false;

    float hpFrac = 1.0f;
    if (target->maxHP > 0) {
        hpFrac = std::clamp(static_cast<float>(target->hp) / static_cast<float>(target->maxHP), 0.0f, 1.0f);
    }
    const float hpFactorRange = std::max(0.0f, config.captureHpFactorMax - config.captureHpFactorMin);
    float hpFactor = config.captureHpFactorMin + (1.0f - hpFrac) * hpFactorRange;
    if (target->fainting || (!target->alive && target->fainting)) {
        hpFactor *= std::max(0.0f, config.captureFaintBonus);
    }

    float chance = baseRate * std::max(0.0f, ballMult) * hpFactor;
    chance = std::clamp(chance, config.captureMinChance, config.captureMaxChance);

    // Shake logic: three checks with p = chance^(1/3)
    const float shakeP = std::pow(chance, 1.0f / 3.0f);
    int shakes = 0;
    auto roll = [&](float p) {
        if (rng) return engine::random::nextFloat01(*rng) <= p;
        return (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) <= p;
    };

    for (int i = 0; i < 3; ++i) {
        if (roll(shakeP)) {
            ++shakes;
        } else {
            break;
        }
    }

    const bool success = (shakes >= 3);

    target->captureInProgress = true;
    target->captureScale = 1.0f;
    target->captureTintStrength = 0.0f;
    target->isMoving = false;
    target->committedDest = {-1,-1};
    target->attackTimerSec = 0.0f;

    ensurePokeballModel();

    if (log) {
        log->catchInfo("Threw pokeball at " + Capitalize(target->name) +
            " (Lv" + std::to_string(target->level) + ")");
    }

    CaptureAttempt attempt;
    attempt.targetId = target->id;
    attempt.success = success;
    attempt.shakes = shakes;
    attempt.name = target->name;
    attempt.level = target->level;
    attempt.throwDur = 0.35f;
    attempt.absorbDur = 0.35f;
    attempt.shakeDur = std::max(0.2f, config.captureAttemptSec);
    attempt.resolveDur = 0.35f;

    attempt.targetPos = target->position;
    attempt.targetPos.y = 0.15f;
    if (throwOrigin) {
        attempt.startPos = *throwOrigin;
    } else {
        attempt.startPos = target->position + glm::vec3(0.0f, 0.0f, -config.cellSize * 3.0f);
        attempt.startPos.y = 0.45f;
    }
    attempt.ballPos = attempt.startPos;
    attempt.ballImpactScale = std::max(0.05f, config.captureBallScale);
    attempt.ballStartScale = std::max(attempt.ballImpactScale, config.captureBallScaleStart);
    attempt.ballBaseScale = attempt.ballImpactScale;
    attempt.ballScale = attempt.ballStartScale;
    attempt.ballYawDeg = 0.0f;
    attempt.phase = CaptureAttempt::Phase::Throw;
    attempt.phaseTime = 0.0f;
    attempt.timeLeftSec = 1.0f;

    captureAttempts.push_back(attempt);

    return true;
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

void GameWorld::spawnPokemon(const std::string& pokemonName,
                             const glm::vec3& startPos,
                             PokemonSide side,
                             int level)
{
    if (!data) {
        std::cerr << "[GameWorld] GameDataDb not set. Call GameWorld::setData() during init.\n";
        return;
    }

    const PokemonStats* stats = data->pokemon.getStats(pokemonName);
    if (!stats) {
        std::cerr << "[GameWorld] No config found for Pokémon: " << pokemonName << "\n";
        return;
    }

    std::string path = "assets/models/" + stats->model;
    std::shared_ptr<Model> sharedModel;
    if (!resources) {
        std::cerr << "[GameWorld] Resource service not set; cannot load model: " << path << "\n";
        if (renderEnabled) return;
    } else {
        sharedModel = resources->getModel(path);
    }

    PokemonInstance inst;
    inst.id = PokemonInstance::getNextUnitID();
    inst.name = pokemonName;
    inst.position = startPos;
    inst.model = sharedModel;

    inst.rotation = glm::vec3(0.0f, (side == PokemonSide::Player ? 180.0f : 0.0f), 0.0f);
    inst.side = side;

    inst.baseHp = stats->hp;
    inst.baseAttack = stats->attack;
    inst.baseMovementSpeed = stats->movementSpeed;
    inst.types = stats->types;
    inst.baseExp = stats->baseExp;

    applyLevelScaling(inst, level, false);
    applyLoadoutForLevel(inst, false);

    inst.animTimeSec = 0.0f;

    // ✅ NEW: animset-v2/v3 roles/groups/categories support (optional file)
    AnimSet::applyAnimSetOverrides(inst, path, data ? &data->flyers : nullptr);

    // Start looped animations in sync across all units
    inst.animTimeSec = sharedLoopAnimTimeSec;

    pokemons.push_back(inst);
    if (side == PokemonSide::Player) {
        mergeTriplesForPlayer();
    }

    std::cout << "[GameWorld] Spawned " << pokemonName
              << " (ID: " << inst.id
              << ", L" << inst.level
              << ", HP: " << inst.hp << "/" << inst.maxHP
              << ", ATK: " << inst.attack
              << ", SPD: " << inst.movementSpeed
              << ", FAST: " << (inst.fastMove.empty() ? "-" : inst.fastMove)
              << ", CHARGED: " << (inst.chargedMove.empty() ? "-" : inst.chargedMove)
              << ", Ecap: " << inst.maxEnergy
              << ")\n";
}

glm::vec3 GameWorld::gridToWorld(int col, int row) const {
    const auto& cfg = config;
    float boardOriginX = -((cfg.cols * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    float boardOriginZ = -((cfg.rows * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    return { boardOriginX + col * cfg.cellSize, 0.0f, boardOriginZ + row * cfg.cellSize };
}

void GameWorld::spawnPokemonAtGrid(const std::string& pokemonName,
                                   int col, int row,
                                   PokemonSide side,
                                   int level)
{
    spawnPokemon(pokemonName, gridToWorld(col, row), side, level);
}

void GameWorld::addToBench(const std::string& pokemonName, int level)
{
    if (!data) {
        std::cerr << "[GameWorld] GameDataDb not set. Call GameWorld::setData() during init.\n";
        return;
    }

    const PokemonStats* stats = data->pokemon.getStats(pokemonName);
    if (!stats) {
        std::cerr << "[GameWorld] No config found for Pokémon: " << pokemonName << "\n";
        return;
    }

    std::string path = "assets/models/" + stats->model;
    std::shared_ptr<Model> sharedModel;
    if (!resources) {
        std::cerr << "[GameWorld] Resource service not set; cannot load model: " << path << "\n";
        if (renderEnabled) return;
    } else {
        sharedModel = resources->getModel(path);
    }

    PokemonInstance inst;
    inst.id = PokemonInstance::getNextUnitID();
    inst.name = pokemonName;
    inst.model = sharedModel;

    inst.rotation = glm::vec3(0.0f, 180.0f, 0.0f);
    inst.side = PokemonSide::Player;

    inst.baseHp = stats->hp;
    inst.baseAttack = stats->attack;
    inst.baseMovementSpeed = stats->movementSpeed;
    inst.types = stats->types;
    inst.baseExp = stats->baseExp;

    applyLevelScaling(inst, level, false);
    applyLoadoutForLevel(inst, false);

    int slot = static_cast<int>(benchPokemons.size());
    const float slotSize = config.cellSize;
    const int benchSlots = std::max(1, config.benchSlots);
    slot = std::min(slot, benchSlots - 1);

    const float totalWidth = benchSlots * slotSize;
    const float startX = -totalWidth * 0.5f;
    const float startZ = (config.rows * config.cellSize) * 0.5f + 0.5f;

    const float x = startX + slotSize * 0.5f + slot * slotSize;
    const float z = startZ + slotSize * 0.5f;
    inst.position = glm::vec3(x, 0.0f, z);

    inst.animTimeSec = 0.0f;

    // ✅ NEW: animset-v2/v3 roles/groups/categories support (optional file)
    AnimSet::applyAnimSetOverrides(inst, path, data ? &data->flyers : nullptr);

    // Start looped animations in sync across all units
    inst.animTimeSec = sharedLoopAnimTimeSec;

    benchPokemons.push_back(inst);
    mergeTriplesForPlayer();

    std::cout << "[GameWorld] Benched " << pokemonName
              << " (ID: " << inst.id
              << " L" << inst.level
              << ", FAST: " << (inst.fastMove.empty() ? "-" : inst.fastMove)
              << ", CHARGED: " << (inst.chargedMove.empty() ? "-" : inst.chargedMove)
              << ")\n";
}

const PokemonInstance* GameWorld::getPokemonByName(const std::string& name) const {
    for (const auto& p : pokemons) {
        if (p.name == name) return &p;
    }
    return nullptr;
}



PokemonInstance* GameWorld::findUnitById(int unitId) {
    for (auto& p : pokemons) {
        if (p.id == unitId) return &p;
    }
    for (auto& b : benchPokemons) {
        if (b.id == unitId) return &b;
    }
    return nullptr;
}

const PokemonInstance* GameWorld::findUnitById(int unitId) const {
    for (const auto& p : pokemons) {
        if (p.id == unitId) return &p;
    }
    for (const auto& b : benchPokemons) {
        if (b.id == unitId) return &b;
    }
    return nullptr;
}
std::vector<PokemonInstance>& GameWorld::getPokemons() { return pokemons; }
std::vector<PokemonInstance>& GameWorld::getBenchPokemons() { return benchPokemons; }

void GameWorld::update(float dt)
{
    // Shared clock so all units loop idle/walk in sync
    sharedLoopAnimTimeSec += dt;

    auto tickPokemonAnim = [&](PokemonInstance& p) {
        if (!p.model) return;
        if (p.fainting) {
            updateFaint(p, dt);
            return;
        }
        if (!p.alive) return;

        p.fastChainTimerSec = std::max(0.0f, p.fastChainTimerSec - dt);

        // attack one-shot has priority (only used when attackTimerSec > 0)
        if (p.attackTimerSec > 0.0f) {
            const int atkIdx = (p.currentAttackAnimIndex >= 0) ? p.currentAttackAnimIndex : p.animAttack1Index;

            if (p.activeAnimIndex != atkIdx) {
                p.activeAnimIndex = atkIdx;
                p.animTimeSec = 0.0f;
            }

            // run timer down
            p.attackTimerSec = std::max(0.0f, p.attackTimerSec - dt);


            // Combat trace: enabled via env (PAC_TRACE_ALL / PAC_TRACE_COMBAT)
            const bool traceCombat = DebugTrace::combat(p.name, p.chainedFastMove);
            if (traceCombat) {
                static std::unordered_map<int, float> prevAtkTimer;
                static std::unordered_map<int, float> prevAnimTime;
                const float prevT = prevAtkTimer[p.id];
                const float prevA = prevAnimTime[p.id];
                prevAtkTimer[p.id] = p.attackTimerSec;
                prevAnimTime[p.id] = p.animTimeSec;

                game::log::infoTerminalOnly(log, std::string("[TRACE_COMBAT_TICK] ") + "unit=" + p.name + " move=" + (p.chainedFastMove.empty() ? std::string("-") : p.chainedFastMove) + " " +
                    "id=" + std::to_string(p.id) +
                    " dt=" + std::to_string(dt) +
                    " atkTimer=" + std::to_string(p.attackTimerSec) +
                    " animTime=" + std::to_string(p.animTimeSec) +
                    " activeAnimIdx=" + std::to_string(p.activeAnimIndex) +
                    " curAtkAnimIdx=" + std::to_string(p.currentAttackAnimIndex) +
                    " speed=" + std::to_string(p.attackAnimSpeed) +
                    " prevAtkTimer=" + std::to_string(prevT) +
                    " prevAnimTime=" + std::to_string(prevA));
            }

            // clamp at last frame (avoid looping)
            const float speed = (p.attackAnimSpeed > 0.0f) ? p.attackAnimSpeed : 1.0f;

            float dur = p.model->getAnimationDurationSec(p.activeAnimIndex);
            if (dur > 0.0f) {
                p.animTimeSec = std::min(p.animTimeSec + dt * speed, dur - 0.0001f);

                if (traceCombat && (p.animTimeSec >= dur - 0.00011f)) {
                    game::log::infoTerminalOnly(log, std::string("[TRACE_COMBAT_TICK] clamped_end ") + "unit=" + p.name + " move=" + (p.chainedFastMove.empty() ? std::string("-") : p.chainedFastMove) + " " +
                        "id=" + std::to_string(p.id) +
                        " dur=" + std::to_string(dur) +
                        " animTime=" + std::to_string(p.animTimeSec));
                }
            } else {
                p.animTimeSec += dt * speed;
            }

            // Spawn pending leech seed projectile at the configured spawn time.
            if (p.pendingProjectileActive && !p.pendingProjectileSpawned) {
                if (p.animTimeSec >= p.pendingProjectileSpawnTimeSec) {
                    auto itTgt = std::find_if(pokemons.begin(), pokemons.end(),
                        [&](const PokemonInstance& u){ return u.id == p.pendingProjectileTargetId; });

                    if (itTgt != pokemons.end() && itTgt->alive && !itTgt->captureInProgress) {
                        if (renderEnabled) {
                            leechSeedVfx.emit(p, *itTgt, p.pendingProjectileTravelSec);
                        }
                    }

                    p.pendingProjectileSpawned = true;
                }
            }

            // Apply pending impact (for leech seed and non-damage impacts).
            if (p.pendingImpactActive && !p.pendingImpactApplied) {
                if (p.animTimeSec >= p.pendingImpactTimeSec) {
                    auto itTgt = std::find_if(pokemons.begin(), pokemons.end(),
                        [&](const PokemonInstance& u){ return u.id == p.pendingImpactTargetId; });

                    if (itTgt != pokemons.end() && itTgt->alive && !itTgt->captureInProgress) {
                        if (p.pendingImpactIsGrass) {
                            emitGrassImpactAt(*itTgt);
                        }
                        if (p.pendingImpactIsLeechSeed) {
                            applyLeechSeed(p.id, itTgt->id);
                        }
                    }

                    p.pendingImpactApplied = true;
                    p.pendingImpactActive = false;
                    p.pendingImpactTargetId = -1;
                    p.pendingImpactTimeSec = 0.0f;
                    p.pendingImpactIsGrass = false;
                    p.pendingImpactIsLeechSeed = false;
                }
            }

            // Apply pending damage at the configured hit time (clip-time seconds).
            if (p.pendingDamageActive && !p.pendingDamageApplied) {
                if (p.animTimeSec >= p.pendingDamageHitTimeSec) {
                    auto itTgt = std::find_if(pokemons.begin(), pokemons.end(),
                        [&](const PokemonInstance& u){ return u.id == p.pendingDamageTargetId; });

                        if (itTgt != pokemons.end() && itTgt->alive && !itTgt->captureInProgress) {
                            const int dmg = std::max(0, p.pendingDamageAmount);
                            itTgt->hp = std::max(0, itTgt->hp - dmg);
                            if (!p.pendingDamageMoveName.empty()) {
                                emitMoveImpactByName(p.pendingDamageMoveName, *itTgt, &p);
                            } else {
                                if (dmg > 0 && p.pendingDamageIsGrass) {
                                    emitGrassImpactAt(*itTgt);
                                }
                                if (dmg > 0 && p.pendingDamageIsTackle) {
                                    emitTackleImpactAt(*itTgt, &p);
                                }
                            }
                            if (itTgt->hp <= 0) {
                                handleUnitFaint(*itTgt);
                            }
                    }

                    p.pendingDamageApplied = true;
                    p.pendingDamageMoveName.clear();
                    p.pendingDamageIsGrass = false;
                    p.pendingDamageIsTackle = false;
                    p.pendingProjectileActive = false;
                    p.pendingProjectileSpawned = false;
                    p.pendingProjectileTargetId = -1;
                    p.pendingProjectileSpawnTimeSec = 0.0f;
                    p.pendingProjectileTravelSec = 0.0f;
                }
            }

            // when done, return to locomotion
            if (p.attackTimerSec <= 0.0f) {

                if (traceCombat) {
                    game::log::infoTerminalOnly(log, std::string("[TRACE_COMBAT_TICK] attack_end ") + "unit=" + p.name + " move=" + (p.chainedFastMove.empty() ? std::string("-") : p.chainedFastMove) + " " +
                        "id=" + std::to_string(p.id) +
                        " finalAnimTime=" + std::to_string(p.animTimeSec) +
                        " activeAnimIdx=" + std::to_string(p.activeAnimIndex));
                }
                p.animTimeSec = 0.0f;
                p.attackAnimSpeed = 1.0f; // reset
                p.currentAttackAnimIndex = p.animAttack1Index; // reset
                p.activeAnimIndex = (p.isMoving ? p.animMoveIndex
                                                : (p.usesAirLocomotion ? p.animGroundIdleIndex : p.animIdleIndex));

                p.pendingProjectileActive = false;
                p.pendingProjectileSpawned = false;
                p.pendingProjectileTargetId = -1;
                p.pendingProjectileSpawnTimeSec = 0.0f;
                p.pendingProjectileTravelSec = 0.0f;

                p.pendingImpactActive = false;
                p.pendingImpactApplied = false;
                p.pendingImpactTargetId = -1;
                p.pendingImpactTimeSec = 0.0f;
                p.pendingImpactIsGrass = false;
                p.pendingImpactIsLeechSeed = false;

                p.pendingDamageActive = false;
                p.pendingDamageApplied = false;
                p.pendingDamageTargetId = -1;
                p.pendingDamageAmount = 0;
                p.pendingDamageHitTimeSec = 0.0f;
                p.pendingDamageMoveName.clear();
                p.pendingDamageIsGrass = false;
                p.pendingDamageIsTackle = false;
            }
            return;
        }

        // locomotion (includes optional airborne takeoff/landing visuals)
        FlightLocomotion::tick(p, dt, sharedLoopAnimTimeSec);
    };

    for (auto& p : pokemons) tickPokemonAnim(p);
    for (auto& p : benchPokemons) tickPokemonAnim(p);

    // Gameplay effects (XP / leech seed) should always update.
    updateLeechSeedStatus(dt);
    updateCaptureAttempts(dt);

    if (!renderEnabled) return;

    // Tail fire VFX: init once, then update every frame
    if (!tailFireVfxInitialized) {
        // Currently only configured for Charmander (via filter + cfg section).
        tailFireVfx.setNameFilterCaseInsensitive("charmander");

        TailFireVFX::Config c; // defaults
        TailFireVFXConfigDB::get().ensureLoaded();          // assets/config/tail_fire_vfx.cfg
        TailFireVFXConfigDB::get().applyIfAny("charmander", c);

        tailFireVfx.setConfig(c);
        tailFireVfxInitialized = true;
    }

    tailFireVfx.update(dt, pokemons, benchPokemons);

    if (!grassImpactVfxInitialized) {
        GrassImpactVFX::Config c; // defaults
        grassImpactVfx.setConfig(c);
        grassImpactVfxInitialized = true;
    }

    if (!tackleImpactVfxInitialized) {
        TackleImpactVFX::Config c; // defaults
        tackleImpactVfx.setConfig(c);
        tackleImpactVfxInitialized = true;
    }

    grassImpactVfx.update(dt);
    tackleImpactVfx.update(dt);

    if (!leechSeedVfxInitialized) {
        LeechSeedProjectileVFX::Config c; // defaults
        leechSeedVfx.setConfig(c);
        leechSeedVfxInitialized = true;
    }

    leechSeedVfx.update(dt);

    if (!healPlusVfxInitialized) {
        HealPlusVFX::Config c; // defaults
        healPlusVfx.setConfig(c);
        healPlusVfxInitialized = true;
    }

    if (!leechSeedDrainVfxInitialized) {
        LeechSeedDrainVFX::Config c; // defaults
        leechSeedDrainVfx.setConfig(c);
        leechSeedDrainVfxInitialized = true;
    }

    if (!growlWaveVfxInitialized) {
        GrowlWaveVFX::Config c; // defaults
        // We now resolve the origin from head/jaw nodes, so keep built-in offsets minimal.
        c.spawnForwardOffset = 0.0f;
        c.spawnHeightOffset = 0.0f;
        growlWaveVfx.setConfig(c);
        growlWaveVfxInitialized = true;
    }

    if (!clawSwipeVfxInitialized) {
        ClawSwipeVFX::Config c; // defaults
        clawSwipeVfx.setConfig(c);
        clawSwipeVfxInitialized = true;
    }

    if (!aquaSwooshVfxInitialized) {
        AquaSwooshVFX::Config c; // defaults
        aquaSwooshVfx.setConfig(c);
        aquaSwooshVfxInitialized = true;
    }

    healPlusVfx.update(dt);
    leechSeedDrainVfx.update(dt);
    growlWaveVfx.update(dt);
    clawSwipeVfx.update(dt);
    aquaSwooshVfx.update(dt);
}

void GameWorld::updateCaptureAttempts(float dt) {
    if (captureAttempts.empty()) return;

    std::vector<int> removeIds;

    for (auto& attempt : captureAttempts) {
        attempt.phaseTime = std::max(0.0f, attempt.phaseTime + dt);
        PokemonInstance* target = findUnitById(attempt.targetId);

        switch (attempt.phase) {
            case CaptureAttempt::Phase::Throw: {
                const float dur = std::max(0.05f, attempt.throwDur);
                const float t = std::clamp(attempt.phaseTime / dur, 0.0f, 1.0f);
                attempt.ballPos = glm::mix(attempt.startPos, attempt.targetPos, t);
                const float arc = std::max(0.1f, config.cellSize * 0.9f);
                attempt.ballPos.y += std::sin(3.1415926f * t) * arc;
                attempt.ballYawDeg += dt * 720.0f;
                attempt.ballScale = glm::mix(attempt.ballStartScale, attempt.ballImpactScale, t);

                if (t >= 1.0f) {
                    attempt.phase = CaptureAttempt::Phase::Absorb;
                    attempt.phaseTime = 0.0f;
                }
                break;
            }
            case CaptureAttempt::Phase::Absorb: {
                const float dur = std::max(0.05f, attempt.absorbDur);
                const float t = std::clamp(attempt.phaseTime / dur, 0.0f, 1.0f);
                attempt.ballPos = attempt.targetPos;
                attempt.ballPos.y = 0.1f;
                attempt.ballScale = attempt.ballImpactScale;

                if (target) {
                    target->captureScale = 1.0f - t;
                    target->captureTintStrength = 1.0f;
                }

                if (t >= 1.0f) {
                    attempt.phase = CaptureAttempt::Phase::Shake;
                    attempt.phaseTime = 0.0f;
                    attempt.shakesEmitted = 0;
                }
                break;
            }
            case CaptureAttempt::Phase::Shake: {
                const int totalShakes = std::max(0, attempt.shakes);
                const float perShake = std::max(0.2f, attempt.shakeDur);
                const float totalDur = (totalShakes > 0) ? (perShake * totalShakes) : perShake;

                const int currentShake = (totalShakes > 0)
                    ? std::min(totalShakes, static_cast<int>(attempt.phaseTime / perShake) + 1)
                    : 0;
                while (attempt.shakesEmitted < currentShake) {
                    attempt.shakesEmitted++;
                    if (log) {
                        if (attempt.shakesEmitted == 1) log->catchInfo("The ball shook once!");
                        if (attempt.shakesEmitted == 2) log->catchInfo("The ball shook twice!");
                        if (attempt.shakesEmitted == 3) log->catchInfo("The ball shook three times!");
                    }
                }

                const float wobble = std::sin(attempt.phaseTime * 16.0f) * (config.cellSize * 0.04f);
                attempt.ballPos = attempt.targetPos;
                attempt.ballPos.x += wobble;
                attempt.ballPos.y = 0.05f;
                attempt.ballYawDeg = std::sin(attempt.phaseTime * 18.0f) * 20.0f;
                attempt.ballScale = attempt.ballImpactScale;

                if (attempt.phaseTime >= totalDur) {
                    attempt.phase = CaptureAttempt::Phase::Resolve;
                    attempt.phaseTime = 0.0f;
                    if (log) {
                        if (attempt.success) {
                            log->catchInfo("Gotcha! " + Capitalize(attempt.name) + " was caught!");
                        } else {
                            log->catchInfo(Capitalize(attempt.name) + " broke free!");
                        }
                    }
                }
                break;
            }
            case CaptureAttempt::Phase::Resolve: {
                const float dur = std::max(0.05f, attempt.resolveDur);
                const float t = std::clamp(attempt.phaseTime / dur, 0.0f, 1.0f);
                attempt.ballPos = attempt.targetPos;
                attempt.ballPos.y = 0.05f;

                if (attempt.success) {
                    attempt.ballScale = attempt.ballImpactScale * (1.0f - t);
                    if (t >= 1.0f) {
                        addToBench(attempt.name, attempt.level);
                        if (target) removeIds.push_back(target->id);
                        attempt.timeLeftSec = 0.0f;
                    }
                } else {
                    attempt.ballScale = attempt.ballImpactScale * (1.0f + t * 0.6f);
                    if (t >= 1.0f) {
                        if (target) {
                            if (renderEnabled) {
                                emitTackleImpactAt(*target, nullptr);
                            }
                            target->captureInProgress = false;
                            target->captureScale = 1.0f;
                            target->captureTintStrength = 0.0f;
                            if (!target->alive) {
                                target->alive = true;
                                target->hp = std::max(1, target->hp);
                                target->fainting = false;
                                target->fadeOutTimerSec = 0.0f;
                                target->visualScale = 1.0f;
                                target->activeAnimIndex = target->animIdleIndex;
                                target->animTimeSec = 0.0f;
                            }
                        }
                        attempt.timeLeftSec = 0.0f;
                    }
                }
                break;
            }
        }
    }

    if (!removeIds.empty()) {
        pokemons.erase(
            std::remove_if(pokemons.begin(), pokemons.end(),
                [&](const PokemonInstance& u) {
                    return std::find(removeIds.begin(), removeIds.end(), u.id) != removeIds.end();
                }),
            pokemons.end()
        );
    }

    captureAttempts.erase(
        std::remove_if(captureAttempts.begin(), captureAttempts.end(),
            [](const CaptureAttempt& a) { return a.timeLeftSec <= 0.0f; }),
        captureAttempts.end()
    );
}

void GameWorld::ensurePokeballModel() {
    if (pokeballModelLoaded) return;
    if (!resources) return;
    pokeballModel = resources->getModel("assets/models/pokeball.glb");
    pokeballModelLoaded = (pokeballModel != nullptr);
}

void GameWorld::drawAll(const Camera3D& camera, BoardRenderer& boardRenderer)
{
    lastViewMatrix = camera.getViewMatrix();
    hasLastViewMatrix = true;

    boardRenderer.draw(camera);
    boardRenderer.drawBench(camera);

    auto drawPokemonList = [&](const std::vector<PokemonInstance>& list) {
        for (const auto& instance : list) {
            if (!instance.model) continue;
            if (!instance.alive && !instance.fainting) continue;

            float scaleFactor = instance.model->getScaleFactor() *
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
        for (const auto& attempt : captureAttempts) {
            if (attempt.timeLeftSec <= 0.0f) continue;
            float scaleFactor = pokeballModel->getScaleFactor() * std::max(0.0f, attempt.ballScale);
            glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
            glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(attempt.ballYawDeg), glm::vec3(0, 1, 0));
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), attempt.ballPos);
            glm::mat4 instanceTransform = translation * rotationY * scale;
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

std::vector<HealthBarData> GameWorld::getHealthBarData(const Camera3D& camera, int screenWidth, int screenHeight) const
{
    return BuildHealthBarData(pokemons, benchPokemons, camera, screenWidth, screenHeight, config);
}

glm::vec3 GameWorld::getNearestEnemyPosition(const PokemonInstance& unit) const
{
    float closestDist = std::numeric_limits<float>::max();
    glm::vec3 closestPos = unit.position;

    for (const auto& other : pokemons) {
        if (!other.alive || other.captureInProgress || other.side == unit.side) continue;
        float d = glm::distance(unit.position, other.position);
        if (d < closestDist) {
            closestDist = d;
            closestPos = other.position;
        }
    }

    return closestPos;
}

void GameWorld::emitGrassImpactAt(const PokemonInstance& target)
{
    if (!renderEnabled) return;
    if (!grassImpactVfxInitialized) {
        GrassImpactVFX::Config c; // defaults
        grassImpactVfx.setConfig(c);
        grassImpactVfxInitialized = true;
    }

    const glm::vec3 base = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);
    grassImpactVfx.emitAt(base);
}

void GameWorld::emitTackleImpactAt(const PokemonInstance& target, const PokemonInstance* attacker)
{
    if (!renderEnabled) return;
    if (!tackleImpactVfxInitialized) {
        TackleImpactVFX::Config c; // defaults
        tackleImpactVfx.setConfig(c);
        tackleImpactVfxInitialized = true;
    }

    glm::vec3 base = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);

    if (attacker && target.model && target.model->hasBounds()) {
        glm::vec3 from = attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f);
        glm::vec3 dir = from - base;
        dir.y = 0.0f;
        const float len = glm::length(dir);
        if (len > 0.0001f) {
            dir /= len;
            const float scale = target.model->getScaleFactor();
            const float radius = target.model->getBoundsRadiusHorizontal();
            const float edge = radius * scale * tackleImpactVfx.getConfig().impactEdgeOffset;
            base += dir * edge;
        }
    }

    tackleImpactVfx.emitAt(base);
}

void GameWorld::emitMoveImpactByName(const std::string& moveName,
                                     const PokemonInstance& target,
                                     const PokemonInstance* attacker)
{
    if (!renderEnabled) return;

    const std::string move = Lower(moveName);
    if (move.empty()) return;

    if (move == "tackle") {
        emitTackleImpactAt(target, attacker);
        return;
    }

    if (move == "vine_whip" || move == "leech_seed") {
        emitGrassImpactAt(target);
        return;
    }

    auto makeForward = [&]() -> glm::vec3 {
        if (attacker) {
            glm::vec3 d = target.position - attacker->position;
            d.y = 0.0f;
            const float len = glm::length(d);
            if (len > 0.0001f) return d / len;
        }
        return glm::vec3(0.0f, 0.0f, 1.0f);
    };

    if (move == "growl") {
        if (!growlWaveVfxInitialized) {
            GrowlWaveVFX::Config c;
            c.spawnForwardOffset = 0.0f;
            c.spawnHeightOffset = 0.0f;
            growlWaveVfx.setConfig(c);
            growlWaveVfxInitialized = true;
        }

        const glm::vec3 forward = makeForward();
        glm::vec3 origin = attacker
            ? (attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f))
            : (target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f));

        if (attacker) {
            const glm::vec3 fwdXZ = SafeForwardXZ(forward);
            const glm::vec3 renderPos = attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f);
            const float worldScale = (attacker->model ? attacker->model->getScaleFactor() : 1.0f) *
                                     std::max(0.0f, attacker->visualScale) *
                                     std::max(0.0f, attacker->captureScale);

            // Stable fallback near mouth/head area in world-space.
            glm::vec3 fallbackOrigin = renderPos + glm::vec3(0.0f, 0.14f, 0.0f);
            fallbackOrigin += fwdXZ * 0.10f;

            static constexpr std::array<const char*, 12> kGrowlNodeCandidates = {
                "EffMouth01", "effmouth01",
                "mouth", "Mouth",
                "head", "Head",
                "jaw", "Jaw",
                "Nose", "nose",
                "neck", "Neck"
            };

            glm::vec3 mouthWorld(0.0f);
            bool resolvedFromNode = false;
            if (TryResolveAnimatedNodeWorld(*attacker, kGrowlNodeCandidates, mouthWorld)) {
                origin = mouthWorld;
                resolvedFromNode = true;
            } else {
                origin = fallbackOrigin;
            }

            // Safety checks: reject pathological node transforms that place the origin
            // underground or behind the caster (seen on some rigs/clips).
            if (resolvedFromNode) {
                const glm::vec3 planarDelta = glm::vec3(origin.x - renderPos.x, 0.0f, origin.z - renderPos.z);
                const float planarDist2 = glm::dot(planarDelta, planarDelta);
                const float maxPlanar = std::max(0.30f, config.cellSize * 0.9f);
                const bool tooLow = origin.y < (renderPos.y + 0.04f);
                const bool tooFar = planarDist2 > (maxPlanar * maxPlanar);
                const bool behind = glm::dot(planarDelta, fwdXZ) < -0.05f;
                if (tooLow || tooFar || behind) {
                    origin = fallbackOrigin;
                }
            }

            const std::string species = Lower(attacker->name);
            float speciesGrowlYOffset = 0.0f;
            float speciesForwardBonus = 0.0f;
            if (species == "bulbasaur") {
                // Bulbasaur mouth sits slightly lower than generic anchors.
                speciesGrowlYOffset = -0.01f;
                speciesForwardBonus = 0.03f;
            }

            float forwardPush = 0.08f + speciesForwardBonus;
            if (attacker->model && attacker->model->hasBounds()) {
                const float r = attacker->model->getBoundsRadiusHorizontal() * worldScale;
                forwardPush = std::clamp(r * 0.38f + speciesForwardBonus, 0.08f, 0.18f);
            }

            origin += glm::vec3(0.0f, speciesGrowlYOffset, 0.0f);
            origin += fwdXZ * forwardPush;

            // Final vertical guardrail against extreme node-space values.
            const float minGrowlY = renderPos.y + 0.06f;
            const float maxGrowlY = renderPos.y + std::max(0.28f, config.cellSize * 0.42f);
            origin.y = std::clamp(origin.y, minGrowlY, maxGrowlY);
        }

        growlWaveVfx.emitFrom(origin, forward, hasLastViewMatrix ? &lastViewMatrix : nullptr);
        return;
    }

    if (move == "scratch" || move == "metal_claw") {
        if (!clawSwipeVfxInitialized) {
            ClawSwipeVFX::Config c;
            clawSwipeVfx.setConfig(c);
            clawSwipeVfxInitialized = true;
        }
        const bool metallic = (move == "metal_claw");
        const glm::vec3 base = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);
        clawSwipeVfx.emitAt(base, makeForward(), metallic);
        return;
    }

    if (move == "tail_whip" || move == "bubble" || move == "water_gun") {
        if (!aquaSwooshVfxInitialized) {
            AquaSwooshVFX::Config c;
            aquaSwooshVfx.setConfig(c);
            aquaSwooshVfxInitialized = true;
        }
        AquaSwooshVFX::Style style = AquaSwooshVFX::Style::TailWhip;
        if (move == "bubble") style = AquaSwooshVFX::Style::Bubble;
        if (move == "water_gun") style = AquaSwooshVFX::Style::WaterGun;

        const glm::vec3 base =
            ((move == "tail_whip") && attacker)
            ? (attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f))
            : (target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f));
        aquaSwooshVfx.emitAt(base, makeForward(), style);
        return;
    }
}

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
            int sap = (int)std::round((float)target.maxHP * pct);
            sap = std::max(leechSeedConfig.minSap, sap);
            if (sap <= 0) break;

            // Apply sap damage
            target.hp = std::max(0, target.hp - sap);
            if (target.hp <= 0) {
                handleUnitFaint(target);
            }

            // VFX: drain dots to source
            {
                const glm::vec3 tpos = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);
                const glm::vec3 spos = source->position + glm::vec3(0.0f, source->visualYOffset, 0.0f);

                const float dist = glm::distance(tpos, spos);
                float travelSec = dist / std::max(0.1f, drainSpeed);
                travelSec = std::clamp(travelSec, minTravel, maxTravel);

                if (renderEnabled) {
                    leechSeedDrainVfx.emitBetween(tpos, spos, travelSec);
                }

                const float healMult = std::max(0.0f, leechSeedConfig.healMultiplier);
                int heal = (int)std::round((float)sap * healMult);
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
                        if (renderEnabled) {
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
