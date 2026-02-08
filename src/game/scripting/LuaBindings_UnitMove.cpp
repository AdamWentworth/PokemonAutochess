// src/game/scripting/LuaBindings_UnitMove.cpp
#include <string>

#include "LuaBindings.h"
#include "game/scripting/ScriptAPI.h"

void registerLuaBindings_UnitMove(sol::state& lua, ScriptAPI& api) {
    lua.set_function("unit_fast_move", [&api](int unitId) -> std::string {
        return api.getUnitFastMove(unitId);
    });

    lua.set_function("unit_charged_move", [&api](int unitId) -> std::string {
        return api.getUnitChargedMove(unitId);
    });

    lua.set_function("move_get", [&api, &lua](const std::string& name) {
        sol::state_view L(lua);
        sol::table t = L.create_table();
        auto move = api.getMove(name);
        if (!move.has_value()) return t;

        t["name"]        = move->name;
        t["type"]        = move->type;
        t["kind"]        = move->kind;
        t["cooldownSec"] = move->cooldownSec;
        t["power"]       = move->power;
        t["range"]       = move->range;
        t["energyGain"]  = move->energyGain;
        t["energyCost"]  = move->energyCost;
        if (move->status.valid) {
            sol::table s = L.create_table();
            s["effect"]      = move->status.effect;
            s["magnitude"]   = move->status.magnitude;
            s["durationSec"] = move->status.durationSec;
            s["target"]      = move->status.target;
            t["status"] = s;
        }
        return t;
    });
}
