#include "game/scripting/ScriptAPI.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <string>

#include "game/GameWorld.h"
#include "game/config/GameDataDb.h"
#include "game/logging/DebugTrace.h"
#include "game/logging/ScratchPerfTrace.h"
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

    using Clock = std::chrono::steady_clock;
    const bool traceScratch =
        game::scratch_trace::shouldTrace(
            services_.engineServices,
            moveName.has_value() ? *moveName : std::string_view{});
    const auto traceStart = Clock::now();
    double lookupMs = 0.0;
    double contextMs = 0.0;
    double animationMs = 0.0;
    double immediateMs = 0.0;

    const GameDataDb* data = world_->getData();
    auto& units = world_->getPokemons();

    const auto attackerIt =
        std::find_if(units.begin(), units.end(), [&](const PokemonInstance& unit) { return unit.id == attackerId; });
    const auto targetIt =
        std::find_if(units.begin(), units.end(), [&](const PokemonInstance& unit) { return unit.id == targetId; });
    if (traceScratch) {
        lookupMs = std::chrono::duration<double, std::milli>(Clock::now() - traceStart).count();
    }

    if (attackerIt == units.end() || targetIt == units.end()) return -1;
    if (!isCombatActive(*attackerIt)) return targetIt->hp;
    if (targetIt->captureInProgress) return targetIt->hp;

    const auto contextStart = traceScratch ? Clock::now() : Clock::time_point{};
    const scriptapi::combat::DamageContext ctx =
        scriptapi::combat::buildDamageContext(*attackerIt, moveName, kind, data);
    if (traceScratch) {
        contextMs = std::chrono::duration<double, std::milli>(Clock::now() - contextStart).count();
    }

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

    const auto animationStart = traceScratch ? Clock::now() : Clock::time_point{};
    int resultHp = targetIt->hp;
    const bool handledByAnimation =
        scriptapi::combat::tryBeginAttackAnimation(*attackerIt,
                                                   *world_,
                                                   *targetIt,
                                                   targetId,
                                                   amount,
                                                   cadenceSec,
                                                   ctx,
                                                   data,
                                                   &services_.log,
                                                   trace,
                                                   resultHp);
    if (traceScratch) {
        animationMs =
            std::chrono::duration<double, std::milli>(Clock::now() - animationStart).count();
    }
    if (handledByAnimation) {
        if (traceScratch) {
            std::ostringstream scratchLog;
            scratchLog << std::fixed << std::setprecision(2)
                       << "attacker=" << attackerId
                       << " target=" << targetId
                       << " amount=" << amount
                       << " kind=" << ctx.kindLower
                       << " handled_by_animation=1"
                       << " result_hp=" << resultHp
                       << " lookup=" << lookupMs << "ms"
                       << " ctx=" << contextMs << "ms"
                       << " anim=" << animationMs << "ms"
                       << " immediate=" << immediateMs << "ms"
                       << " total=" <<
                           std::chrono::duration<double, std::milli>(Clock::now() - traceStart).count()
                       << "ms";
            game::scratch_trace::emit(&services_.log, "script_apply_damage", scratchLog.str());
        }
        return resultHp;
    }

    const auto immediateStart = traceScratch ? Clock::now() : Clock::time_point{};
    resultHp =
        scriptapi::combat::applyImmediateDamage(*world_, *attackerIt, *targetIt, amount, ctx, trace);
    if (traceScratch) {
        immediateMs =
            std::chrono::duration<double, std::milli>(Clock::now() - immediateStart).count();
        std::ostringstream scratchLog;
        scratchLog << std::fixed << std::setprecision(2)
                   << "attacker=" << attackerId
                   << " target=" << targetId
                   << " amount=" << amount
                   << " kind=" << ctx.kindLower
                   << " handled_by_animation=0"
                   << " result_hp=" << resultHp
                   << " lookup=" << lookupMs << "ms"
                   << " ctx=" << contextMs << "ms"
                   << " anim=" << animationMs << "ms"
                   << " immediate=" << immediateMs << "ms"
                   << " total=" <<
                          std::chrono::duration<double, std::milli>(Clock::now() - traceStart).count()
                   << "ms";
        game::scratch_trace::emit(&services_.log, "script_apply_damage", scratchLog.str());
    }
    return resultHp;
}
