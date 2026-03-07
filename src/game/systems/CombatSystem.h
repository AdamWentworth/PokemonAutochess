// CombatSystem.h
#pragma once
#include "engine/core/ecs/ISystem.h"
#include "engine/core/ecs/Entity.h"
#include "game/GameServices.h"
#include "game/scripting/ScriptAPI.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class GameWorld;

class CombatSystem : public engine::ecs::ISystem {
public:
    explicit CombatSystem(GameWorld* world, GameServices& services, engine::ecs::Entity combatEntity);
    ~CombatSystem();
    const char* debugName() const override { return "combat"; }
    void update(engine::ecs::World& world, float deltaTime) override;

private:
    struct CombatTuning {
        float fastCdMult = 2.25f;
        float chargedCdMult = 2.25f;
        float minFastRequestSec = 1.0f;
        float minChargedRequestSec = 1.0f;
        float attackSpeedScale = 0.65f;
        float speedBaseline = 1.0f;
        float speedMin = 0.35f;
        float speedMax = 3.0f;
        float damagePowerMult = 1.0f;
        float damageAtkScale = 0.50f;
        int damageMin = 1;
        float energyGainMult = 0.75f;
        int energyGainOnHit = 6;
        float energyGainOnHitMult = 1.0f;
        int statStageMin = -6;
        int statStageMax = 6;
        std::unordered_map<std::string, float> moveSpeedMult{
            {"vine_whip", 1.60f},
        };
    };

    struct TypeMultiplierBucket {
        float best = 1.0f;
        float great = 1.0f;
        float neutral = 1.0f;
        float bad = 1.0f;
        float terrible = 1.0f;
        float worst = 1.0f;
    };

    GameWorld* gameWorld;
    GameServices& services;
    engine::ecs::Entity combatEntity;
    std::unique_ptr<ScriptAPI> api;
    CombatTuning tuning_;
    TypeMultiplierBucket chargedTypeMult_{4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.0f};
    TypeMultiplierBucket fastTypeMult_{2.56f, 1.6f, 1.0f, 0.625f, 0.391f, 0.244f};
    std::unordered_map<std::string, std::unordered_map<std::string, float>> typeChart_;
    std::unordered_map<int, float> timers_;
    std::unordered_map<int, int> attackStage_;
    std::unordered_map<int, int> defenseStage_;
    std::unordered_map<int, bool> chargedPending_;
    std::unordered_map<int, int> focusedTarget_;
    std::unordered_map<int, int> lockedTarget_;
    int combatLogLevel_ = 1;

    void loadConfig();
    void pruneCombatState(const std::vector<ScriptAPI::CombatUnitSnapshot>& units);
    float random01() const;
    void combatEmit(const std::string& msg, int minLevel) const;
    bool targetExistsForLock(int targetId) const;
    bool canStartAttackNow(int unitId) const;
    float unitAttackSpeedFactor(const ScriptAPI::CombatUnitSnapshot& unit) const;
    int unitAttackStat(int unitId) const;
    float statStageMultiplier(int stage) const;
    float attackerDamageStageMult(int unitId) const;
    float targetDamageTakenStageMult(int unitId) const;
    float typeChartProduct(const std::string& moveType, int targetId) const;
    float resolveTypeBucketMultiplier(const std::string& kind, const std::string& tier) const;
    float typeMultiplier(const std::string& moveName,
                         const std::string& moveType,
                         const std::string& kind,
                         int targetId) const;
    std::string effectivenessTag(const std::string& moveName,
                                 const std::string& moveType,
                                 int targetId) const;
    void maybeEmitEffectiveness(const std::string& tag) const;
    int computeDamage(int attackerId, int targetId, int power) const;
    int applyTypeMultiplier(const std::string& moveName,
                            const std::string& moveType,
                            const std::string& kind,
                            int targetId,
                            int damage) const;
    int computeEnergyGain(int base, float mult) const;
    float minRequestSec(int attackerId,
                        const std::string& moveName,
                        const std::string& kind,
                        float base) const;
    bool isAttackCycleActive(const ScriptAPI::CombatUnitSnapshot& unit) const;
    std::string displayName(int unitId) const;
    bool applyStageDrop(const std::string& effect, int targetId, float stageCount);
    void markChargedPendingIfReady(const ScriptAPI::CombatUnitSnapshot& unit);
    bool fireCharged(const ScriptAPI::CombatUnitSnapshot& unit, int targetId);
    bool fireFast(const ScriptAPI::CombatUnitSnapshot& unit, int targetId);
};
