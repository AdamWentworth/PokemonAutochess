-- scripts/states/shared/combat_route_shared.lua

local route_catalog = dofile("scripts/states/shared/route_catalog.lua")
local mode_utils = dofile("scripts/states/shared/mode_utils.lua")

local function script_path(script_name)
    return "scripts/states/" .. script_name .. ".lua"
end

local function is_victory()
    local units = world_list_units() or {}
    local any_enemy_alive = false
    local any_player_alive = false

    for i = 1, #units do
        local u = units[i]
        if u.side == "Enemy" then
            if u.alive or u.captureInProgress then
                any_enemy_alive = true
            end
        elseif u.side == "Player" then
            if u.alive then
                any_player_alive = true
            end
        end
    end

    return any_player_alive and (not any_enemy_alive)
end

local M = {}

function M.install(route_id, mode, target)
    local route = route_catalog.copy(route_id)
    if not route then
        error("Unknown route id: " .. tostring(route_id))
    end

    target = target or _ENV

    local resolved_mode = mode_utils.normalize(mode)
    local next_shop_path = script_path(route.next_shop)
    local transitioned = false

    if resolved_mode == "classic" then
        dofile("scripts/states/shared/round_economy.lua")
    end

    target.get_message = function()
        return route.message
    end

    target.get_enemies = function()
        local enemies = {}
        for i = 1, #route.enemies do
            local e = route.enemies[i]
            enemies[i] = {
                name = e.name,
                gridCol = e.gridCol,
                gridRow = e.gridRow,
                level = e.level
            }
        end
        return enemies
    end

    target.get_combat_balance = function()
        return {
            playerDamageMult = route.combat_balance.playerDamageMult,
            enemyDamageMult = route.combat_balance.enemyDamageMult,
            playerDamageTakenMult = route.combat_balance.playerDamageTakenMult,
            enemyDamageTakenMult = route.combat_balance.enemyDamageTakenMult
        }
    end

    target.on_update = function(dt)
        if transitioned then
            return
        end

        if resolved_mode == "classic" then
            if classic_try_finish_round(next_shop_path) then
                transitioned = true
            end
            return
        end

        if is_victory() then
            transitioned = true
            emit(route.clear_message)
            pop_state()
            push_state(next_shop_path)
        end
    end
end

return M
