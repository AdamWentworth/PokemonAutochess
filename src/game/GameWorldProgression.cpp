#include "GameWorld.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "GameConfig.h"

#include "config/GameDataDb.h"
#include "config/MovesConfigLoader.h"
#include "config/PokemonConfigLoader.h"

#include "logging/LoggerUtil.h"

namespace {

std::string capitalize(std::string s) {
    if (s.empty()) return s;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

const LoadoutEntry* pickLoadoutForLevel(const PokemonStats& stats, int level) {
    const LoadoutEntry* best = nullptr;
    for (const auto& [lvl, loadout] : stats.loadoutByLevel) {
        if (lvl <= level) best = &loadout;
        else break;
    }
    return best;
}

}  // namespace

void GameWorld::applyLevelScaling(PokemonInstance& inst, int level, bool preserveHp) const {
    const auto& cfg = config;
    const int useLevel = (level <= 0) ? cfg.baseLevel : level;

    inst.level = useLevel;

    float hpRatio = 1.0f;
    if (preserveHp && inst.maxHP > 0) {
        hpRatio = std::clamp(static_cast<float>(inst.hp) / static_cast<float>(inst.maxHP), 0.0f, 1.0f);
    }

    const float hpMult = std::pow(1.0f + cfg.perLevelHpBoost, static_cast<float>(useLevel - 1));
    const float atkMult = std::pow(1.0f + cfg.perLevelAttackBoost, static_cast<float>(useLevel - 1));
    const float spdMult = std::pow(1.0f + cfg.perLevelSpeedBoost, static_cast<float>(useLevel - 1));

    inst.maxHP = static_cast<int>(std::round(static_cast<float>(inst.baseHp) * hpMult));
    if (preserveHp) {
        inst.hp = std::clamp(static_cast<int>(std::round(static_cast<float>(inst.maxHP) * hpRatio)), 1, inst.maxHP);
    } else {
        inst.hp = inst.maxHP;
    }
    inst.attack = static_cast<int>(std::round(static_cast<float>(inst.baseAttack) * atkMult));
    inst.movementSpeed = inst.baseMovementSpeed * spdMult;
}

void GameWorld::applyLoadoutForLevel(PokemonInstance& inst, bool preserveEnergy) const {
    if (!data) {
        inst.fastMove.clear();
        inst.chargedMove.clear();
        inst.maxEnergy = 100;
        if (!preserveEnergy) inst.energy = 0;
        return;
    }

    const PokemonStats* stats = data->pokemon.getStats(inst.name);
    if (!stats) {
        inst.fastMove.clear();
        inst.chargedMove.clear();
        inst.maxEnergy = 100;
        if (!preserveEnergy) inst.energy = 0;
        return;
    }

    const LoadoutEntry* loadout = pickLoadoutForLevel(*stats, inst.level);
    if (loadout) {
        inst.fastMove = loadout->fast;
        inst.chargedMove = loadout->hasCharged ? loadout->charged : std::string();
    } else {
        inst.fastMove.clear();
        inst.chargedMove.clear();
    }

    inst.maxEnergy = 100;
    if (!inst.chargedMove.empty()) {
        if (const auto* move = data->moves.getMove(inst.chargedMove)) {
            if (move->energyCost > 0) inst.maxEnergy = move->energyCost;
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
    const int level = std::max(1, unit.level);
    for (int l = 1; l < level; ++l) {
        total += xpToNextLevel(l);
    }
    total += std::max(0, unit.xp);
    return std::max(0, total);
}

void GameWorld::levelProgressFromTotalXp(int totalXp, int& outLevel, int& outXp) const {
    int remaining = std::max(0, totalXp);
    int level = 1;
    const int maxLevel = config.xpMaxLevel;

    while (true) {
        if (maxLevel > 0 && level >= maxLevel) {
            outLevel = maxLevel;
            outXp = std::min(remaining, xpToNextLevel(maxLevel));
            return;
        }

        const int needed = xpToNextLevel(level);
        if (remaining < needed) {
            outLevel = level;
            outXp = remaining;
            return;
        }
        remaining -= needed;
        ++level;
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
    for (auto& [name, candidates] : bySpecies) {
        if (candidates.size() < 3) continue;
        std::sort(candidates.begin(), candidates.end(), [](const UnitRef& a, const UnitRef& b) {
            if (a.totalXp != b.totalXp) return a.totalXp > b.totalXp;
            if (a.onBoard != b.onBoard) return a.onBoard;
            return a.id < b.id;
        });
        species = name;
        refs = {candidates[0], candidates[1], candidates[2]};
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
    tryApplyEvolution(*keeper);

    if (log) {
        game::log::info(log, "Merged 3x " + capitalize(species) + " -> Lv" + std::to_string(keeper->level));
    }
    return true;
}

void GameWorld::mergeTriplesForPlayer() {
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
        tryApplyEvolution(unit);
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

    std::sort(recipients.begin(), recipients.end(), [](const PokemonInstance* a, const PokemonInstance* b) {
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
