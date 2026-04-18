#include "CombatSystem.h"

#include "engine/core/Environment.h"
#include "engine/core/EngineServices.h"
#include "engine/core/IAssetStore.h"
#include "engine/core/Paths.h"
#include "engine/core/ecs/World.h"
#include "game/GameWorld.h"
#include "game/animation/FlightLocomotion.h"
#include "game/config/GameDataDb.h"
#include "game/logging/ScratchPerfTrace.h"
#include "game/config/MovesConfigLoader.h"
#include "game/PhaseState.h"
#include "game/scripting/LuaBindings_Internal.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_set>

namespace {

constexpr float kAttackReadyEps = 0.0001f;
constexpr float kMissChance = 0.10f;
constexpr float kCritChance = 0.125f;
constexpr float kCritMult = 1.5f;

using ScratchTraceClock = std::chrono::steady_clock;

double elapsedMsLocal(ScratchTraceClock::time_point start, ScratchTraceClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

std::string normalizeVirtualPath(std::string path) {
    std::string root = engine::paths::dataRoot();
    std::replace(root.begin(), root.end(), '\\', '/');
    std::replace(path.begin(), path.end(), '\\', '/');
    if (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    if (!root.empty() && path.rfind(root + "/", 0) == 0) {
        path = path.substr(root.size() + 1);
    }
    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
        path.erase(path.begin());
    }
    return path;
}

std::optional<sol::table> loadLuaTableFromStore(sol::state& lua,
                                                const std::string& path,
                                                engine::IAssetStore& assets,
                                                std::string& outErr) {
    std::string text;
    std::string err;
    const std::string virt = normalizeVirtualPath(path);
    if (!assets.readText(virt, text, &err)) {
        outErr = err.empty() ? ("Failed to read " + virt) : err;
        return std::nullopt;
    }

    sol::load_result chunk = lua.load(text);
    if (!chunk.valid()) {
        sol::error e = chunk;
        outErr = e.what();
        return std::nullopt;
    }

    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error e = r;
        outErr = e.what();
        return std::nullopt;
    }
    if (r.return_count() < 1) {
        outErr = "Lua config did not return a table";
        return std::nullopt;
    }

    sol::object obj = r.get<sol::object>();
    if (!obj.is<sol::table>()) {
        outErr = "Lua config did not return a table";
        return std::nullopt;
    }
    return obj.as<sol::table>();
}

bool isCombatActive(const PokemonInstance& unit) {
    return unit.alive && !unit.captureInProgress;
}

bool canIssueAttack(const PokemonInstance& unit) {
    if (!isCombatActive(unit)) return false;
    if (unit.usesAirLocomotion && FlightLocomotion::isAirborne(unit)) return false;
    return true;
}

std::string titleCaseName(std::string name) {
    if (name.empty()) return "Unknown";
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

template <typename T>
void assignNumericField(const sol::table& table, const char* key, T& outValue) {
    const sol::object obj = table[key];
    if (!obj.valid()) return;
    if constexpr (std::is_integral_v<T>) {
        if (obj.is<int>()) {
            outValue = obj.as<int>();
        } else if (obj.is<double>()) {
            outValue = static_cast<T>(std::lround(obj.as<double>()));
        }
    } else {
        if (obj.is<double>()) {
            outValue = static_cast<T>(obj.as<double>());
        } else if (obj.is<int>()) {
            outValue = static_cast<T>(obj.as<int>());
        }
    }
}

std::string typeTierFromProduct(float product) {
    if (product <= 0.0f) return "worst";
    if (product >= 4.0f) return "best";
    if (product >= 2.0f) return "great";
    if (product <= 0.25f) return "terrible";
    if (product <= 0.5f) return "bad";
    return "neutral";
}

}  // namespace

CombatSystem::CombatSystem(GameWorld* world, GameServices& svc, engine::ecs::Entity combatEntity_)
    : gameWorld(world), services(svc), combatEntity(combatEntity_) {
    api = std::make_unique<ScriptAPI>(gameWorld, /*manager*/ nullptr, services);
    loadConfig();

    const auto rawLevel = engine::env::get("PAC_COMBAT_LOG_LEVEL");
    if (rawLevel.has_value()) {
        try {
            combatLogLevel_ = std::clamp(std::stoi(*rawLevel), 0, 2);
        } catch (...) {
            combatLogLevel_ = 1;
        }
    }
}

CombatSystem::~CombatSystem() = default;

void CombatSystem::loadConfig() {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

    {
        std::string err;
        const auto tuningTable =
            loadLuaTableFromStore(lua, "scripts/config/combat_tuning.lua", services.assets, err);
        if (!tuningTable.has_value()) {
            std::cerr << "[CombatSystem] combat tuning fallback: " << err << "\n";
        } else {
            assignNumericField(*tuningTable, "FAST_CD_MULT", tuning_.fastCdMult);
            assignNumericField(*tuningTable, "CHARGED_CD_MULT", tuning_.chargedCdMult);
            assignNumericField(*tuningTable, "MIN_FAST_REQUEST_SEC", tuning_.minFastRequestSec);
            assignNumericField(*tuningTable, "MIN_CHARGED_REQUEST_SEC", tuning_.minChargedRequestSec);
            assignNumericField(*tuningTable, "ATTACK_SPEED_SCALE", tuning_.attackSpeedScale);
            assignNumericField(*tuningTable, "SPEED_BASELINE", tuning_.speedBaseline);
            assignNumericField(*tuningTable, "SPEED_MIN", tuning_.speedMin);
            assignNumericField(*tuningTable, "SPEED_MAX", tuning_.speedMax);
            assignNumericField(*tuningTable, "DAMAGE_POWER_MULT", tuning_.damagePowerMult);
            assignNumericField(*tuningTable, "DAMAGE_ATK_SCALE", tuning_.damageAtkScale);
            assignNumericField(*tuningTable, "DAMAGE_MIN", tuning_.damageMin);
            assignNumericField(*tuningTable, "ENERGY_GAIN_MULT", tuning_.energyGainMult);
            assignNumericField(*tuningTable, "ENERGY_GAIN_ON_HIT", tuning_.energyGainOnHit);
            assignNumericField(*tuningTable, "ENERGY_GAIN_ON_HIT_MULT", tuning_.energyGainOnHitMult);
            assignNumericField(*tuningTable, "STAT_STAGE_MIN", tuning_.statStageMin);
            assignNumericField(*tuningTable, "STAT_STAGE_MAX", tuning_.statStageMax);

            const sol::object moveSpeedObj = (*tuningTable)["MOVE_SPEED_MULT"];
            if (moveSpeedObj.valid() && moveSpeedObj.is<sol::table>()) {
                for (const auto& kv : moveSpeedObj.as<sol::table>()) {
                    if (!kv.first.is<std::string>()) continue;
                    const sol::object value = kv.second;
                    if (!(value.is<double>() || value.is<int>())) continue;
                    tuning_.moveSpeedMult[toLowerCopy(kv.first.as<std::string>())] =
                        value.is<double>() ? static_cast<float>(value.as<double>())
                                           : static_cast<float>(value.as<int>());
                }
            }
        }
    }

    {
        std::string err;
        const auto typeTable =
            loadLuaTableFromStore(lua, "scripts/config/type_chart.lua", services.assets, err);
        if (!typeTable.has_value()) {
            std::cerr << "[CombatSystem] type chart fallback: " << err << "\n";
            return;
        }

        const sol::object multipliersObj = (*typeTable)["multipliers"];
        if (multipliersObj.valid() && multipliersObj.is<sol::table>()) {
            const sol::table multipliers = multipliersObj.as<sol::table>();
            const sol::object chargedObj = multipliers["charged"];
            if (chargedObj.valid() && chargedObj.is<sol::table>()) {
                const sol::table charged = chargedObj.as<sol::table>();
                assignNumericField(charged, "best", chargedTypeMult_.best);
                assignNumericField(charged, "great", chargedTypeMult_.great);
                assignNumericField(charged, "neutral", chargedTypeMult_.neutral);
                assignNumericField(charged, "bad", chargedTypeMult_.bad);
                assignNumericField(charged, "terrible", chargedTypeMult_.terrible);
                assignNumericField(charged, "worst", chargedTypeMult_.worst);
            }
            const sol::object fastObj = multipliers["fast"];
            if (fastObj.valid() && fastObj.is<sol::table>()) {
                const sol::table fast = fastObj.as<sol::table>();
                assignNumericField(fast, "best", fastTypeMult_.best);
                assignNumericField(fast, "great", fastTypeMult_.great);
                assignNumericField(fast, "neutral", fastTypeMult_.neutral);
                assignNumericField(fast, "bad", fastTypeMult_.bad);
                assignNumericField(fast, "terrible", fastTypeMult_.terrible);
                assignNumericField(fast, "worst", fastTypeMult_.worst);
            }
        }

        const sol::object chartObj = (*typeTable)["chart"];
        if (chartObj.valid() && chartObj.is<sol::table>()) {
            typeChart_.clear();
            for (const auto& attackEntry : chartObj.as<sol::table>()) {
                if (!attackEntry.first.is<std::string>() || !attackEntry.second.is<sol::table>()) continue;
                const std::string attackType = toLowerCopy(attackEntry.first.as<std::string>());
                auto& row = typeChart_[attackType];
                for (const auto& defendEntry : attackEntry.second.as<sol::table>()) {
                    if (!defendEntry.first.is<std::string>()) continue;
                    const sol::object value = defendEntry.second;
                    if (!(value.is<double>() || value.is<int>())) continue;
                    row[toLowerCopy(defendEntry.first.as<std::string>())] =
                        value.is<double>() ? static_cast<float>(value.as<double>())
                                           : static_cast<float>(value.as<int>());
                }
            }
        }
    }
}

void CombatSystem::pruneCombatState(const std::vector<ScriptAPI::CombatUnitSnapshot>& units) {
    std::unordered_set<int> aliveIds;
    aliveIds.reserve(units.size());
    for (const auto& unit : units) {
        aliveIds.insert(unit.id);
    }

    const auto eraseMissing = [&](auto& map) {
        for (auto it = map.begin(); it != map.end();) {
            if (aliveIds.find(it->first) == aliveIds.end()) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    };

    eraseMissing(timers_);
    eraseMissing(attackStage_);
    eraseMissing(defenseStage_);
    eraseMissing(chargedPending_);
    eraseMissing(focusedTarget_);
    eraseMissing(lockedTarget_);
}

float CombatSystem::random01() const {
    const float denom = static_cast<float>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<float>(services.rng.nextU32()) / denom;
}

void CombatSystem::combatEmit(const std::string& msg, int minLevel) const {
    if (!api) return;
    if (combatLogLevel_ < minLevel) return;
    api->emit(msg, std::nullopt);
}

bool CombatSystem::targetExistsForLock(int targetId) const {
    if (!gameWorld || targetId < 0) return false;
    const auto* unit = gameWorld->findUnitById(targetId);
    if (!unit) return false;
    return unit->alive || unit->fainting || unit->captureInProgress;
}

bool CombatSystem::canStartAttackNow(int unitId) const {
    if (!gameWorld) return false;
    const auto* unit = gameWorld->findUnitById(unitId);
    if (!unit || !canIssueAttack(*unit)) return false;
    return unit->attackTimerSec <= kAttackReadyEps;
}

float CombatSystem::unitAttackSpeedFactor(const ScriptAPI::CombatUnitSnapshot& unit) const {
    float speed = unit.speed;
    if (speed <= 0.0f && api) {
        speed = api->getUnitSpeed(unit.id);
    }
    if (speed <= 0.0f) speed = tuning_.speedBaseline;

    float baseline = tuning_.speedBaseline;
    if (baseline <= 0.0f) baseline = 1.0f;
    float factor = (speed / baseline) * tuning_.attackSpeedScale;
    return std::clamp(factor, tuning_.speedMin, tuning_.speedMax);
}

int CombatSystem::unitAttackStat(int unitId) const {
    if (!gameWorld) return 0;
    const auto* unit = gameWorld->findUnitById(unitId);
    return unit ? unit->attack : 0;
}

float CombatSystem::statStageMultiplier(int stage) const {
    const int clampedStage = std::clamp(stage, tuning_.statStageMin, tuning_.statStageMax);
    if (clampedStage >= 0) {
        return (2.0f + static_cast<float>(clampedStage)) / 2.0f;
    }
    return 2.0f / (2.0f - static_cast<float>(clampedStage));
}

float CombatSystem::attackerDamageStageMult(int unitId) const {
    const auto it = attackStage_.find(unitId);
    return statStageMultiplier(it != attackStage_.end() ? it->second : 0);
}

float CombatSystem::targetDamageTakenStageMult(int unitId) const {
    const auto it = defenseStage_.find(unitId);
    const float defenseMult = statStageMultiplier(it != defenseStage_.end() ? it->second : 0);
    if (defenseMult <= 0.0f) return 1.0f;
    return 1.0f / defenseMult;
}

float CombatSystem::typeChartProduct(const std::string& moveType, int targetId) const {
    if (moveType.empty() || !gameWorld) return 1.0f;

    const auto moveIt = typeChart_.find(toLowerCopy(moveType));
    if (moveIt == typeChart_.end()) return 1.0f;

    const auto* target = gameWorld->findUnitById(targetId);
    if (!target) return 1.0f;

    float product = 1.0f;
    for (const auto& defendType : target->types) {
        const auto rowIt = moveIt->second.find(toLowerCopy(defendType));
        if (rowIt == moveIt->second.end()) continue;
        if (rowIt->second <= 0.0f) return 0.0f;
        product *= rowIt->second;
    }
    return product;
}

float CombatSystem::resolveTypeBucketMultiplier(const std::string& kind, const std::string& tier) const {
    const TypeMultiplierBucket& bucket =
        (toLowerCopy(kind) == "charged") ? chargedTypeMult_ : fastTypeMult_;

    if (tier == "best") return bucket.best;
    if (tier == "great") return bucket.great;
    if (tier == "bad") return bucket.bad;
    if (tier == "terrible") return bucket.terrible;
    if (tier == "worst") return bucket.worst;
    return bucket.neutral;
}

float CombatSystem::typeMultiplier(const std::string& moveName,
                                   const std::string& moveType,
                                   const std::string& kind,
                                   int targetId) const {
    if (toLowerCopy(moveName) == "leech_seed") return 1.0f;
    if (moveType.empty()) return 1.0f;
    const float product = typeChartProduct(moveType, targetId);
    return resolveTypeBucketMultiplier(kind, typeTierFromProduct(product));
}

std::string CombatSystem::effectivenessTag(const std::string& moveName,
                                           const std::string& moveType,
                                           int targetId) const {
    if (toLowerCopy(moveName) == "leech_seed") return "neutral";
    if (moveType.empty()) return "neutral";
    const std::string tier = typeTierFromProduct(typeChartProduct(moveType, targetId));
    if (tier == "best" || tier == "great") return "super";
    if (tier == "bad" || tier == "terrible") return "not_very";
    if (tier == "worst") return "immune";
    return "neutral";
}

void CombatSystem::maybeEmitEffectiveness(const std::string& tag) const {
    if (tag == "super") {
        combatEmit("It's super effective!", 2);
    } else if (tag == "not_very") {
        combatEmit("It's not very effective...", 2);
    } else if (tag == "immune") {
        combatEmit("It doesn't affect the target...", 2);
    }
}

int CombatSystem::computeDamage(int attackerId, int targetId, int power) const {
    if (power <= 0) return 0;

    const float bonus = static_cast<float>(unitAttackStat(attackerId)) * tuning_.damageAtkScale;
    float damage = (static_cast<float>(power) * tuning_.damagePowerMult) + bonus;
    if (api) {
        damage *= api->getDamageMultiplier(attackerId, targetId);
    }
    damage *= attackerDamageStageMult(attackerId);

    int rounded = static_cast<int>(std::floor(damage + 0.5f));
    if (rounded < tuning_.damageMin) rounded = tuning_.damageMin;
    return rounded;
}

int CombatSystem::applyTypeMultiplier(const std::string& moveName,
                                      const std::string& moveType,
                                      const std::string& kind,
                                      int targetId,
                                      int damage) const {
    if (damage <= 0) return 0;
    const float mult = typeMultiplier(moveName, moveType, kind, targetId);
    const float stageAdjusted =
        static_cast<float>(damage) * mult * targetDamageTakenStageMult(targetId);
    const int rounded = static_cast<int>(std::floor(stageAdjusted + 0.5f));
    return std::max(0, rounded);
}

int CombatSystem::computeEnergyGain(int base, float mult) const {
    if (base <= 0) return 0;
    const int rounded = static_cast<int>(std::floor((static_cast<float>(base) * mult) + 0.5f));
    return std::max(0, rounded);
}

float CombatSystem::minRequestSec(int attackerId,
                                  const std::string& moveName,
                                  const std::string& kind,
                                  float base) const {
    if (!api) return base;
    const float overrideValue = api->attackMinRequestSec(attackerId, moveName, kind);
    return (overrideValue > base) ? overrideValue : base;
}

bool CombatSystem::isAttackCycleActive(const ScriptAPI::CombatUnitSnapshot& unit) const {
    const auto it = timers_.find(unit.id);
    if (it != timers_.end() && it->second > kAttackReadyEps) return true;
    return !canStartAttackNow(unit.id);
}

std::string CombatSystem::displayName(int unitId) const {
    if (!gameWorld) return "Unknown";
    const auto* unit = gameWorld->findUnitById(unitId);
    return unit ? titleCaseName(unit->name) : "Unknown";
}

bool CombatSystem::applyStageDrop(const std::string& effect, int targetId, float stageCount) {
    const int drop = std::max(1, static_cast<int>(std::floor(stageCount)));
    if (effect == "attack_down") {
        const int current = attackStage_[targetId];
        const int next = std::clamp(current - drop, tuning_.statStageMin, tuning_.statStageMax);
        if (next == current) return false;
        attackStage_[targetId] = next;
        combatEmit(displayName(targetId) + "'s Attack fell!", 1);
        return true;
    }
    if (effect == "defense_down") {
        const int current = defenseStage_[targetId];
        const int next = std::clamp(current - drop, tuning_.statStageMin, tuning_.statStageMax);
        if (next == current) return false;
        defenseStage_[targetId] = next;
        combatEmit(displayName(targetId) + "'s Defense fell!", 1);
        return true;
    }
    return false;
}

void CombatSystem::markChargedPendingIfReady(const ScriptAPI::CombatUnitSnapshot& unit) {
    if (unit.chargedMove.empty() || !api) return;
    const MoveData* move = services.dataDb.moves.getMove(unit.chargedMove);
    if (!move) return;

    const int currentEnergy = api->getEnergy(unit.id);
    const int maxEnergy = std::max(0, api->getMaxEnergy(unit.id));
    int needed = move->energyCost;
    if (needed <= 0) needed = maxEnergy;
    if (needed <= 0) return;

    if (currentEnergy >= needed) {
        chargedPending_[unit.id] = true;
    }
}

bool CombatSystem::fireCharged(const ScriptAPI::CombatUnitSnapshot& unit, int targetId) {
    if (!api || unit.chargedMove.empty() || targetId < 0) return false;

    const MoveData* move = services.dataDb.moves.getMove(unit.chargedMove);
    if (!move) return false;

    const int currentEnergy = api->getEnergy(unit.id);
    const int maxEnergy = std::max(0, api->getMaxEnergy(unit.id));
    int needed = move->energyCost;
    if (needed <= 0) needed = maxEnergy;
    if (needed <= 0 || currentEnergy < needed) return false;
    if (!canStartAttackNow(unit.id)) return false;

    const float speed = unitAttackSpeedFactor(unit);
    float cd = std::max(0.05f, move->cooldownSec);
    cd = (cd * tuning_.chargedCdMult) / speed;
    cd = std::max(cd, minRequestSec(unit.id, move->name, "charged", tuning_.minChargedRequestSec) / speed);

    api->setEnergy(unit.id, currentEnergy - needed);
    combatEmit(displayName(unit.id) + " used " + move->name + "!", 1);

    const std::string statusEffect =
        move->status.valid ? toLowerCopy(move->status.effect) : std::string();
    if (statusEffect == "attack_down" || statusEffect == "defense_down") {
        std::vector<int> targets = api->enemiesAdjacent(unit.id);
        if (targets.empty() && targetId >= 0) {
            targets.push_back(targetId);
        }

        int anchorTarget = targetId;
        if (anchorTarget < 0 && !targets.empty()) {
            anchorTarget = targets.front();
        }
        if (anchorTarget >= 0) {
            api->applyDamage(unit.id, anchorTarget, 0, cd, move->name, "charged");
        }

        int affected = 0;
        const float stageCount = move->status.valid ? move->status.magnitude : 1.0f;
        for (int enemyId : targets) {
            if (applyStageDrop(statusEffect, enemyId, stageCount)) {
                ++affected;
            }
        }
        if (affected <= 0) {
            combatEmit("But it failed!", 2);
        }

        timers_[unit.id] = cd;
        chargedPending_[unit.id] = false;
        lockedTarget_[unit.id] = targetId;
        return true;
    }

    int damage = computeDamage(unit.id, targetId, move->power);
    damage = applyTypeMultiplier(move->name, move->type, "charged", targetId, damage);
    if (random01() < kCritChance) {
        damage = static_cast<int>(std::floor(static_cast<float>(damage) * kCritMult + 0.5f));
        combatEmit("A critical hit!", 2);
    }

    const int remainingHp = api->applyDamage(unit.id, targetId, damage, cd, move->name, "charged");
    maybeEmitEffectiveness(effectivenessTag(move->name, move->type, targetId));
    if (remainingHp == 0) {
        combatEmit(displayName(targetId) + " fainted!", 1);
    }

    timers_[unit.id] = cd;
    chargedPending_[unit.id] = false;
    lockedTarget_[unit.id] = targetId;
    return true;
}

bool CombatSystem::fireFast(const ScriptAPI::CombatUnitSnapshot& unit, int targetId) {
    if (!api || unit.fastMove.empty() || targetId < 0) return false;

    const MoveData* move = services.dataDb.moves.getMove(unit.fastMove);
    if (!move) return false;
    if (!canStartAttackNow(unit.id)) return false;

    const bool traceScratch = game::scratch_trace::shouldTrace(services.engineServices, move->name);
    const auto traceStart = ScratchTraceClock::now();
    double prepMs = 0.0;
    double damageMs = 0.0;
    double applyDamageMs = 0.0;
    double onHitEnergyMs = 0.0;
    double selfEnergyMs = 0.0;
    bool missed = false;
    bool crit = false;
    int dealtDamage = 0;
    int remainingHp = -1;

    const float speed = unitAttackSpeedFactor(unit);
    const auto multIt = tuning_.moveSpeedMult.find(toLowerCopy(move->name));
    const float moveSpeedMult = (multIt != tuning_.moveSpeedMult.end()) ? multIt->second : 1.0f;

    float cd = std::max(0.05f, move->cooldownSec);
    cd = (cd * tuning_.fastCdMult) / (speed * moveSpeedMult);
    cd = std::max(cd, minRequestSec(unit.id, move->name, "fast", tuning_.minFastRequestSec) /
                          (speed * moveSpeedMult));

    timers_[unit.id] = cd;
    lockedTarget_[unit.id] = targetId;

    combatEmit(displayName(unit.id) + " used " + move->name + "!", 1);
    if (traceScratch) {
        prepMs = elapsedMsLocal(traceStart, ScratchTraceClock::now());
    }

    if (random01() < kMissChance) {
        missed = true;
        combatEmit("It missed!", 2);
        const auto applyStart = traceScratch ? ScratchTraceClock::now() : ScratchTraceClock::time_point{};
        remainingHp = api->applyDamage(unit.id, targetId, 0, cd, move->name, "fast");
        if (traceScratch) {
            applyDamageMs = elapsedMsLocal(applyStart, ScratchTraceClock::now());
        }
    } else {
        const auto damageStart = traceScratch ? ScratchTraceClock::now() : ScratchTraceClock::time_point{};
        int damage = computeDamage(unit.id, targetId, move->power);
        damage = applyTypeMultiplier(move->name, move->type, "fast", targetId, damage);
        if (random01() < kCritChance) {
            crit = true;
            damage = static_cast<int>(std::floor(static_cast<float>(damage) * kCritMult + 0.5f));
            combatEmit("A critical hit!", 2);
        }
        dealtDamage = damage;
        if (traceScratch) {
            damageMs = elapsedMsLocal(damageStart, ScratchTraceClock::now());
        }

        const auto applyStart = traceScratch ? ScratchTraceClock::now() : ScratchTraceClock::time_point{};
        remainingHp = api->applyDamage(unit.id, targetId, damage, cd, move->name, "fast");
        if (traceScratch) {
            applyDamageMs = elapsedMsLocal(applyStart, ScratchTraceClock::now());
        }
        const int onHitEnergy = computeEnergyGain(tuning_.energyGainOnHit, tuning_.energyGainOnHitMult);
        if (onHitEnergy > 0) {
            const auto onHitStart =
                traceScratch ? ScratchTraceClock::now() : ScratchTraceClock::time_point{};
            api->addEnergy(targetId, onHitEnergy);
            if (traceScratch) {
                onHitEnergyMs = elapsedMsLocal(onHitStart, ScratchTraceClock::now());
            }
        }
        maybeEmitEffectiveness(effectivenessTag(move->name, move->type, targetId));
        if (remainingHp == 0) {
            combatEmit(displayName(targetId) + " fainted!", 1);
        }
    }

    const int gainedEnergy = computeEnergyGain(move->energyGain, tuning_.energyGainMult);
    if (gainedEnergy > 0) {
        const auto selfEnergyStart =
            traceScratch ? ScratchTraceClock::now() : ScratchTraceClock::time_point{};
        api->addEnergy(unit.id, gainedEnergy);
        if (traceScratch) {
            selfEnergyMs = elapsedMsLocal(selfEnergyStart, ScratchTraceClock::now());
        }
    }

    if (traceScratch) {
        std::ostringstream trace;
        trace << std::fixed << std::setprecision(2)
              << "attacker=" << unit.id
              << " target=" << targetId
              << " damage=" << dealtDamage
              << " remaining_hp=" << remainingHp
              << " miss=" << (missed ? 1 : 0)
              << " crit=" << (crit ? 1 : 0)
              << " cd=" << cd
              << " prep=" << prepMs << "ms"
              << " damage_calc=" << damageMs << "ms"
              << " apply=" << applyDamageMs << "ms"
              << " on_hit_energy=" << onHitEnergyMs << "ms"
              << " self_energy=" << selfEnergyMs << "ms"
              << " total=" << elapsedMsLocal(traceStart, ScratchTraceClock::now()) << "ms";
        game::scratch_trace::emit(&services.log, "combat_fire_fast", trace.str());
    }
    return true;
}

void CombatSystem::update(engine::ecs::World& ecsWorld, float deltaTime) {
    using Clock = std::chrono::high_resolution_clock;

    if (!api) return;
    if (!ecsWorld.alive(combatEntity)) return;
    auto* combat = ecsWorld.get<game::CombatActive>(combatEntity);
    if (!combat || !combat->active) return;
    if (!gameWorld || gameWorld->isBoardResizePauseActive()) return;

    EngineFixedPerfBreakdown* fixedBreakdown =
        services.engineServices ? &services.engineServices->frameFixedBreakdown : nullptr;

    const auto planStart = Clock::now();
    const bool scratchTraceMode = game::scratch_trace::isTerminalModeEnabled(services.engineServices);
    bool scratchUnitActive = false;
    bool scratchFastIssued = false;
    int scratchAttackerId = -1;
    int scratchTargetId = -1;
    double listUnitsMsTrace = 0.0;
    double upkeepMsTrace = 0.0;
    double decisionMsTrace = 0.0;
    double facingMsTrace = 0.0;
    double planMsTrace = 0.0;
    double flushMsTrace = 0.0;

    std::vector<ScriptAPI::CombatUnitSnapshot> units = api->listUnitsForCombat();
    pruneCombatState(units);
    if (scratchTraceMode) {
        listUnitsMsTrace =
            std::chrono::duration<double, std::milli>(Clock::now() - planStart).count();
    }

    for (const auto& unit : units) {
        scratchUnitActive = scratchUnitActive || game::scratch_trace::isScratchMove(unit.fastMove);
        timers_[unit.id] = std::max(0.0f, timers_[unit.id] - deltaTime);
        if (chargedPending_.find(unit.id) == chargedPending_.end()) {
            chargedPending_[unit.id] = false;
        }
        if (!isAttackCycleActive(unit)) {
            lockedTarget_.erase(unit.id);
        }
        if (!unit.alive) {
            attackStage_.erase(unit.id);
            defenseStage_.erase(unit.id);
            chargedPending_.erase(unit.id);
            focusedTarget_.erase(unit.id);
            lockedTarget_.erase(unit.id);
        }
    }
    const auto upkeepEnd = Clock::now();
    if (scratchTraceMode) {
        upkeepMsTrace =
            std::chrono::duration<double, std::milli>(upkeepEnd - planStart).count() - listUnitsMsTrace;
    }

    for (const auto& unit : units) {
        const bool adjacent = unit.alive && unit.adjacentEnemyCount > 0;
        const bool cycleLocked = isAttackCycleActive(unit);
        if (!adjacent && !cycleLocked) {
            focusedTarget_.erase(unit.id);
            lockedTarget_.erase(unit.id);
        }
    }

    for (const auto& unit : units) {
        const bool adjacent = unit.alive && unit.adjacentEnemyCount > 0;
        const bool cycleLocked = isAttackCycleActive(unit);
        if (!unit.alive || (!adjacent && !cycleLocked)) continue;

        int targetId = -1;
        if (cycleLocked) {
            const auto it = lockedTarget_.find(unit.id);
            if (it != lockedTarget_.end()) targetId = it->second;
        } else if (adjacent) {
            targetId = unit.bestAdjacentEnemyId;
        }

        if (targetId >= 0) {
            focusedTarget_[unit.id] = targetId;
        } else if (!cycleLocked) {
            focusedTarget_.erase(unit.id);
        }

        const bool targetValid = targetExistsForLock(targetId);
        if (adjacent && !cycleLocked && targetValid) {
            markChargedPendingIfReady(unit);

            if (chargedPending_[unit.id] && timers_[unit.id] <= kAttackReadyEps) {
                if (fireCharged(unit, targetId)) {
                    continue;
                }
            }

            if (!chargedPending_[unit.id] && timers_[unit.id] <= kAttackReadyEps) {
                const bool scratchFastCandidate =
                    game::scratch_trace::isScratchMove(unit.fastMove);
                if (fireFast(unit, targetId) && scratchFastCandidate) {
                    scratchFastIssued = true;
                    scratchAttackerId = unit.id;
                    scratchTargetId = targetId;
                }
            }
        }
    }
    const auto decisionEnd = Clock::now();
    if (scratchTraceMode) {
        decisionMsTrace =
            std::chrono::duration<double, std::milli>(decisionEnd - upkeepEnd).count();
    }

    for (const auto& unit : units) {
        if (!unit.alive) continue;

        const auto targetIt = focusedTarget_.find(unit.id);
        const int targetId = (targetIt != focusedTarget_.end()) ? targetIt->second : -1;
        const bool cycleLocked = isAttackCycleActive(unit);

        if (targetId >= 0 && targetExistsForLock(targetId)) {
            api->faceTarget(unit.id, targetId);
        } else if (!cycleLocked) {
            api->faceEnemy(unit.id, std::nullopt, std::nullopt);
        }
    }
    const auto faceEnd = Clock::now();
    if (scratchTraceMode) {
        facingMsTrace =
            std::chrono::duration<double, std::milli>(faceEnd - decisionEnd).count();
        planMsTrace =
            std::chrono::duration<double, std::milli>(faceEnd - planStart).count();
    }

    if (fixedBreakdown) {
        fixedBreakdown->combatPlanMs += static_cast<float>(
            std::chrono::duration<double, std::milli>(Clock::now() - planStart).count());
    }

    const auto flushStart = Clock::now();
    api->flush();
    if (scratchTraceMode) {
        flushMsTrace =
            std::chrono::duration<double, std::milli>(Clock::now() - flushStart).count();
    }
    if (fixedBreakdown) {
        fixedBreakdown->combatFlushMs += static_cast<float>(
            std::chrono::duration<double, std::milli>(Clock::now() - flushStart).count());
    }

    if (scratchTraceMode && (scratchFastIssued || (scratchUnitActive && planMsTrace >= 4.0))) {
        std::ostringstream trace;
        trace << std::fixed << std::setprecision(2)
              << "units=" << units.size()
              << " scratch_active=" << (scratchUnitActive ? 1 : 0)
              << " scratch_fired=" << (scratchFastIssued ? 1 : 0)
              << " attacker=" << scratchAttackerId
              << " target=" << scratchTargetId
              << " list=" << listUnitsMsTrace << "ms"
              << " upkeep=" << upkeepMsTrace << "ms"
              << " decision=" << decisionMsTrace << "ms"
              << " facing=" << facingMsTrace << "ms"
              << " plan=" << planMsTrace << "ms"
              << " flush=" << flushMsTrace << "ms"
              << " total=" << (planMsTrace + flushMsTrace) << "ms";
        game::scratch_trace::emit(&services.log, "combat_update", trace.str());
    }
}
