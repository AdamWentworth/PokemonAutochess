#pragma once

#include <optional>
#include <string>

#include "game/PokemonInstance.h"

class GameWorld;
struct GameDataDb;
struct MoveData;
namespace LogBus { class Logger; }

namespace scriptapi::combat {

struct DamageContext {
    std::string speciesLower;
    std::string moveLower;
    std::string kindLower;
    const MoveData* moveData = nullptr;
    bool isGrassImpact = false;
    bool isLeechSeed = false;
    bool isTackle = false;
};

struct TraceContext {
    bool enabled = false;
    LogBus::Logger* log = nullptr;
    std::string speciesLower;
    std::string moveLower;
};

DamageContext buildDamageContext(const PokemonInstance& attacker,
                                 const std::optional<std::string>& moveName,
                                 const std::optional<std::string>& kind,
                                 const GameDataDb* data);

void traceLog(const TraceContext& trace, const std::string& msg);

bool tryBeginAttackAnimation(PokemonInstance& attacker,
                             GameWorld& world,
                             PokemonInstance& target,
                             int targetId,
                             int amount,
                             const std::optional<float>& cadenceSec,
                             const DamageContext& ctx,
                             const GameDataDb* data,
                             LogBus::Logger* log,
                             const TraceContext& trace,
                             int& outResultHp);

int applyImmediateDamage(GameWorld& world,
                         PokemonInstance& attacker,
                         PokemonInstance& target,
                         int amount,
                         const DamageContext& ctx,
                         const TraceContext& trace);

}  // namespace scriptapi::combat
