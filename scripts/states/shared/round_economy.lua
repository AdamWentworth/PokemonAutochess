-- scripts/states/shared/round_economy.lua

local function classic_emit_income(result)
    local gold_now = get_money()
    local total = result.total or 0
    emit_gold(string.format("Earned +%dg. Gold: %dg.", total, gold_now))
end

function classic_try_finish_round(next_shop_script)
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

    local won = any_player_alive and (not any_enemy_alive)
    if not won then
        return false
    end

    local result = classic_award_round_income(won)
    classic_emit_income(result)
    emit("Round", "Round cleared!")

    pop_state()
    push_state(next_shop_script)
    return true
end
