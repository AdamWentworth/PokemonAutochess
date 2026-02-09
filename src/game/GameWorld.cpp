// src/game/GameWorld.cpp
#include <cmath>
#include <limits>
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <memory>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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
    const int perFaint = xpFromFaint(dead);
    if (perFaint <= 0) return;

    if (dead.side != PokemonSide::Enemy) return;

    bool anyOppAlive = false;
    for (const auto& u : pokemons) {
        if (!u.alive) continue;
        if (u.side == PokemonSide::Player) { anyOppAlive = true; break; }
    }
    if (!anyOppAlive) return;

    for (auto& u : pokemons) {
        if (!u.alive) continue;
        if (u.side != PokemonSide::Player) continue;
        addXp(u, perFaint);
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
            if (!u.alive) continue;
            if (u.side != PokemonSide::Player) continue;
            u.hp = u.maxHP;
        }
    };

    healList(pokemons);
    healList(benchPokemons);
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

                    if (itTgt != pokemons.end() && itTgt->alive) {
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

                    if (itTgt != pokemons.end() && itTgt->alive) {
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
            if (p.pendingDamageActive && !p.pendingDamageApplied && p.pendingDamageAmount > 0) {
                if (p.animTimeSec >= p.pendingDamageHitTimeSec) {
                    auto itTgt = std::find_if(pokemons.begin(), pokemons.end(),
                        [&](const PokemonInstance& u){ return u.id == p.pendingDamageTargetId; });

                        if (itTgt != pokemons.end() && itTgt->alive) {
                            const int dmg = std::max(0, p.pendingDamageAmount);
                            itTgt->hp = std::max(0, itTgt->hp - dmg);
                            if (dmg > 0 && p.pendingDamageIsGrass) {
                                emitGrassImpactAt(*itTgt);
                            }
                            if (dmg > 0 && p.pendingDamageIsTackle) {
                                emitTackleImpactAt(*itTgt, &p);
                            }
                            if (itTgt->hp <= 0) {
                                handleUnitFaint(*itTgt);
                            }
                    }

                    p.pendingDamageApplied = true;
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

    healPlusVfx.update(dt);
    leechSeedDrainVfx.update(dt);
}

void GameWorld::drawAll(const Camera3D& camera, BoardRenderer& boardRenderer)
{
    boardRenderer.draw(camera);
    boardRenderer.drawBench(camera);

    auto drawPokemonList = [&](const std::vector<PokemonInstance>& list) {
        for (const auto& instance : list) {
            if (!instance.model) continue;
            if (!instance.alive && !instance.fainting) continue;

            float scaleFactor = instance.model->getScaleFactor() * std::max(0.0f, instance.visualScale);

            glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
            glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
            glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
            glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
            glm::vec3 renderPos = instance.position + glm::vec3(0.0f, instance.visualYOffset, 0.0f);
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), renderPos);

            glm::mat4 instanceTransform = translation * rotationY * rotationX * rotationZ * scale;

            instance.model->drawAnimated(camera, instanceTransform, instance.animTimeSec, instance.activeAnimIndex);
        }
    };

    drawPokemonList(pokemons);
    drawPokemonList(benchPokemons);

    // draw particles AFTER opaque models
    tailFireVfx.render(camera);
    grassImpactVfx.render(camera);
    tackleImpactVfx.render(camera);
    leechSeedVfx.render(camera);
    healPlusVfx.render(camera);
    leechSeedDrainVfx.render(camera);
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
        if (!other.alive || other.side == unit.side) continue;
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
