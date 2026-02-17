#include "game/scripting/ScriptAPI.h"

#include <algorithm>
#include <string>

#include "game/GameWorld.h"
#include "game/config/GameDataDb.h"
#include "game/logging/DebugTrace.h"
#include "game/scripting/ScriptAPICombatInternal.h"

namespace {

bool isCombatActive(const PokemonInstance& unit) {
    return unit.alive && !unit.captureInProgress;
}

}  // namespace

int ScriptAPI::applyDamage(int attackerId,
                           int targetId,
                           int amount,
                           const std::optional<float>& cadenceSec,
                           const std::optional<std::string>& moveName,
                           const std::optional<std::string>& kind) {
    if (!world_) return -1;

    const GameDataDb* data = world_->getData();
    auto& units = world_->getPokemons();

    const auto attackerIt =
        std::find_if(units.begin(), units.end(), [&](const PokemonInstance& unit) { return unit.id == attackerId; });
    const auto targetIt =
        std::find_if(units.begin(), units.end(), [&](const PokemonInstance& unit) { return unit.id == targetId; });

    if (attackerIt == units.end() || targetIt == units.end()) return -1;
    if (!isCombatActive(*attackerIt)) return targetIt->hp;
    if (targetIt->captureInProgress) return targetIt->hp;

    const scriptapi::combat::DamageContext ctx =
        scriptapi::combat::buildDamageContext(*attackerIt, moveName, kind, data);

    scriptapi::combat::TraceContext trace;
    trace.enabled = DebugTrace::combat(ctx.speciesLower, ctx.moveLower);
    trace.log = &services_.log;
    trace.speciesLower = ctx.speciesLower;
    trace.moveLower = ctx.moveLower;

    if (trace.enabled) {
        scriptapi::combat::traceLog(
            trace,
            std::string("enter attackerId=") + std::to_string(attackerId) +
                " targetId=" + std::to_string(targetId) +
                " kind=" + ctx.kindLower +
                " move=" + (ctx.moveLower.empty() ? std::string("-") : ctx.moveLower) +
                " amount=" + std::to_string(amount) +
                " cadenceSec_in=" + std::to_string(cadenceSec.value_or(-1.0f)) +
                " atkTimer=" + std::to_string(attackerIt->attackTimerSec) +
                " atkDur=" + std::to_string(attackerIt->attackDurationSec) +
                " activeAnimIdx=" + std::to_string(attackerIt->activeAnimIndex) +
                " curAtkAnimIdx=" + std::to_string(attackerIt->currentAttackAnimIndex) +
                " atkAnimSpeed=" + std::to_string(attackerIt->attackAnimSpeed) +
                " fastChainTimerSec=" + std::to_string(attackerIt->fastChainTimerSec) +
                " chainedFastMove=" +
                (attackerIt->chainedFastMove.empty() ? std::string("-") : attackerIt->chainedFastMove));
    }

    int resultHp = targetIt->hp;
    if (scriptapi::combat::tryBeginAttackAnimation(*attackerIt,
                                                   *world_,
                                                   *targetIt,
                                                   targetId,
                                                   amount,
                                                   cadenceSec,
                                                   ctx,
                                                   data,
                                                   &services_.log,
                                                   trace,
                                                   resultHp)) {
        return resultHp;
    }

    return scriptapi::combat::applyImmediateDamage(*world_, *attackerIt, *targetIt, amount, ctx, trace);
}
