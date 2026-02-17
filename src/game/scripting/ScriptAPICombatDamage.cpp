#include "game/scripting/ScriptAPICombatInternal.h"

#include <algorithm>
#include <string>

#include "game/GameWorld.h"

namespace scriptapi::combat {

int applyImmediateDamage(GameWorld& world,
                         PokemonInstance& attacker,
                         PokemonInstance& target,
                         int amount,
                         const DamageContext& ctx,
                         const TraceContext& trace) {
    const int damage = std::max(0, amount);
    traceLog(trace,
             std::string("damage_apply dmg=") + std::to_string(damage) +
                 " hp_before=" + std::to_string(target.hp));

    target.hp = std::max(0, target.hp - damage);
    if (!ctx.moveLower.empty()) {
        world.emitMoveImpactByName(ctx.moveLower, target, &attacker);
    } else {
        if (damage > 0 && ctx.isGrassImpact) {
            world.emitGrassImpactAt(target);
        }
        if (damage > 0 && ctx.isTackle) {
            world.emitTackleImpactAt(target, &attacker);
        }
    }

    traceLog(trace,
             std::string("damage_result hp_after=") + std::to_string(target.hp) +
                 " targetAlive=" + std::string(target.hp > 0 ? "true" : "false"));

    if (target.hp <= 0) {
        world.handleUnitFaint(target);
    }

    return target.hp;
}

}  // namespace scriptapi::combat
