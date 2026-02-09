-- scripts/states/classic/round_economy.lua

local function classic_emit_income(result)
    local gold_now = get_money()
    local streak_label = result.won and ("W" .. tostring(result.win_streak))
        or ("L" .. tostring(result.loss_streak))
    local summary = string.format(
        "%s +%dg (base %d, interest %d, streak %d) | Gold: %d | %s",
        result.won and "Win!" or "Loss!",
        result.total or 0,
        result.base or 0,
        result.interest or 0,
        result.streak or 0,
        gold_now,
        streak_label
    )
    emit("Gold", summary)
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
