// src/game/scripting/LuaBindings.cpp
#include <glm/glm.hpp>
#include "engine/render/Model.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

#include "LuaBindings.h"

#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/GameStateManager.h"
#include "game/GameConfig.h"

#include "game/animation/FlightLocomotion.h"
#include "game/animation/AttackAnimDebug.h"

#include "game/config/GameDataDb.h"
#include "game/config/AnimSetLoader.h"

#include "game/state/ScriptedState.h"

#include "game/logging/LogBus.h"
#include "game/logging/DebugTrace.h"

#include "LuaBindings_Internal.h"

void registerLuaBindings_UnitMove(sol::state& lua, GameWorld* world, GameStateManager* manager, LogBus::Logger* logger) {
    (void)logger;
lua.set_function("unit_fast_move", [world](int unitId) -> std::string {
        if (!world) return "";
        if (auto* u = world->findUnitById(unitId)) return u->fastMove;
        return "";
    });

    lua.set_function("unit_charged_move", [world](int unitId) -> std::string {
        if (!world) return "";
        if (auto* u = world->findUnitById(unitId)) return u->chargedMove;
        return "";
    });
lua.set_function("move_get", [world, &lua](const std::string& name) {
        sol::state_view L(lua);
        sol::table t = L.create_table();
        const auto* data = world ? world->getData() : nullptr;
        if (!data) return t;
        const auto* md = data->moves.getMove(name);
        if (!md) return t;
        t["name"]        = md->name;
        t["type"]        = md->type;
        t["kind"]        = md->kind;
        t["cooldownSec"] = md->cooldownSec;
        t["power"]       = md->power;
        t["range"]       = md->range;
        t["energyGain"]  = md->energyGain;
        t["energyCost"]  = md->energyCost;
        if (md->status.valid) {
            sol::table s = L.create_table();
            s["effect"]      = md->status.effect;
            s["magnitude"]   = md->status.magnitude;
            s["durationSec"] = md->status.durationSec;
            s["target"]      = md->status.target;
            t["status"] = s;
        }
        return t;
    });
}
