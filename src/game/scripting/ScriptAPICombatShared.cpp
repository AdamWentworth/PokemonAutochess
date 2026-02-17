#include "game/scripting/ScriptAPICombatInternal.h"

#include "game/config/GameDataDb.h"
#include "game/logging/LoggerUtil.h"

#include "LuaBindings_Internal.h"

namespace scriptapi::combat {

DamageContext buildDamageContext(const PokemonInstance& attacker,
                                 const std::optional<std::string>& moveName,
                                 const std::optional<std::string>& kind,
                                 const GameDataDb* data) {
    DamageContext ctx;
    ctx.speciesLower = toLowerCopy(attacker.name);
    ctx.moveLower = moveName ? toLowerCopy(*moveName) : "";
    ctx.kindLower = kind ? toLowerCopy(*kind) : "";

    if (!ctx.moveLower.empty() && data) {
        ctx.moveData = data->moves.getMove(ctx.moveLower);
    }

    if (ctx.kindLower.empty() && ctx.moveData) {
        ctx.kindLower = toLowerCopy(ctx.moveData->kind);
    }
    if (ctx.kindLower.empty()) ctx.kindLower = "fast";

    const std::string moveTypeLower = ctx.moveData ? toLowerCopy(ctx.moveData->type) : std::string();
    const bool isGrassMove = (moveTypeLower == "grass");
    ctx.isLeechSeed = (ctx.moveLower == "leech_seed");
    ctx.isTackle = (ctx.moveLower == "tackle");
    ctx.isGrassImpact = (isGrassMove || ctx.isLeechSeed);
    return ctx;
}

void traceLog(const TraceContext& trace, const std::string& msg) {
    if (!trace.enabled || !trace.log) return;
    game::log::infoTerminalOnly(trace.log,
                                std::string("[TRACE_COMBAT_CPP] ") +
                                    "unit=" + trace.speciesLower + " move=" +
                                    (trace.moveLower.empty() ? std::string("-") : trace.moveLower) +
                                    " " + msg);
}

}  // namespace scriptapi::combat
