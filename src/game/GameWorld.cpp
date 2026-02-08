// src/game/GameWorld.cpp
#include <cmath>
#include <limits>
#include <algorithm>
#include <unordered_map>
#include <iostream>
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
    : config(cfg) {}

void GameWorld::applyLevelScaling(PokemonInstance& inst, int level) const {
    const auto& cfg = config;
    const int useLevel = (level <= 0) ? cfg.baseLevel : level;

    inst.level = useLevel;

    const float mult = std::pow(1.0f + cfg.perLevelBoost, static_cast<float>(useLevel - 1));

    inst.maxHP         = static_cast<int>(std::round(static_cast<float>(inst.baseHp) * mult));
    inst.hp            = inst.maxHP;
    inst.attack        = static_cast<int>(std::round(static_cast<float>(inst.baseAttack) * mult));
    inst.movementSpeed = inst.baseMovementSpeed * mult;
}

static const LoadoutEntry* pickLoadoutForLevel(const PokemonStats& ps, int level) {
    const LoadoutEntry* best = nullptr;

    for (const auto& [lvl, le] : ps.loadoutByLevel) {
        if (lvl <= level) best = &le;
        else break;
    }

    return best;
}

void GameWorld::applyLoadoutForLevel(PokemonInstance& inst) const {
    if (!data) {
        inst.fastMove.clear();
        inst.chargedMove.clear();
        inst.maxEnergy = 100;
        inst.energy = 0;
        return;
    }

    const PokemonStats* ps = data->pokemon.getStats(inst.name);
    if (!ps) {
        inst.fastMove.clear();
        inst.chargedMove.clear();
        inst.maxEnergy = 100;
        inst.energy = 0;
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
    inst.energy = 0;
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
    if (!resources) {
        std::cerr << "[GameWorld] Resource service not set; cannot load model: " << path << "\n";
        return;
    }
    auto sharedModel = resources->getModel(path);

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

    applyLevelScaling(inst, level);
    applyLoadoutForLevel(inst);

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

void GameWorld::addToBench(const std::string& pokemonName)
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
    if (!resources) {
        std::cerr << "[GameWorld] Resource service not set; cannot load model: " << path << "\n";
        return;
    }
    auto sharedModel = resources->getModel(path);

    PokemonInstance inst;
    inst.id = PokemonInstance::getNextUnitID();
    inst.name = pokemonName;
    inst.model = sharedModel;

    inst.rotation = glm::vec3(0.0f, 180.0f, 0.0f);
    inst.side = PokemonSide::Player;

    inst.baseHp = stats->hp;
    inst.baseAttack = stats->attack;
    inst.baseMovementSpeed = stats->movementSpeed;

    applyLevelScaling(inst, -1);
    applyLoadoutForLevel(inst);

    int slot = static_cast<int>(benchPokemons.size());
    float spacing = 1.2f;
    float x = (slot - 4) * spacing + spacing / 2.0f;
    float z = 4.5f;
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
        if (!p.alive || !p.model) return;

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
                            if (itTgt->hp <= 0) {
                                itTgt->hp = 0;
                                itTgt->alive = false;

                            // optional cleanup so dead units don't keep doing leftover animation state
                            itTgt->isMoving = false;
                            itTgt->attackTimerSec = 0.0f;
                            itTgt->attackAnimSpeed = 1.0f;
                            itTgt->currentAttackAnimIndex = itTgt->animAttack1Index;
                            itTgt->pendingAttackAfterLanding = false;
                            itTgt->queuedAttackDurationSec = 0.0f;
                            itTgt->queuedAttackAnimIndex = -1;
                            itTgt->chainedFastMove.clear();
                            itTgt->fastChainTimerSec = 0.0f;
                            itTgt->animIndexCache.clear();
                        }
                        }

                    p.pendingDamageApplied = true;
                    p.pendingDamageIsGrass = false;
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
            }
            return;
        }

        // locomotion (includes optional airborne takeoff/landing visuals)
        FlightLocomotion::tick(p, dt, sharedLoopAnimTimeSec);
    };

    for (auto& p : pokemons) tickPokemonAnim(p);
    for (auto& p : benchPokemons) tickPokemonAnim(p);

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

    grassImpactVfx.update(dt);
}

void GameWorld::drawAll(const Camera3D& camera, BoardRenderer& boardRenderer)
{
    boardRenderer.draw(camera);
    boardRenderer.drawBench(camera);

    auto drawPokemonList = [&](const std::vector<PokemonInstance>& list) {
        for (const auto& instance : list) {
            if (!instance.alive || !instance.model) continue;

            float scaleFactor = instance.model->getScaleFactor();

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
}

std::vector<HealthBarData> GameWorld::getHealthBarData(const Camera3D& camera, int screenWidth, int screenHeight) const
{
    return BuildHealthBarData(pokemons, benchPokemons, camera, screenWidth, screenHeight);
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
    if (!grassImpactVfxInitialized) {
        GrassImpactVFX::Config c; // defaults
        grassImpactVfx.setConfig(c);
        grassImpactVfxInitialized = true;
    }

    const glm::vec3 base = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);
    grassImpactVfx.emitAt(base);
}
