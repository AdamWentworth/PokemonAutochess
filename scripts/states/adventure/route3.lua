-- scripts/states/adventure/route3.lua

function get_message()
    return "Route 3 - Stronger wilds ahead!"
end

function get_enemies()
    return {
        { name = "metapod", gridCol = 1, gridRow = 1, level = 8 },
        { name = "kakuna",  gridCol = 2, gridRow = 1, level = 8 },
        { name = "pidgey",  gridCol = 4, gridRow = 1, level = 9 },
        { name = "kakuna",  gridCol = 5, gridRow = 1, level = 8 },
        { name = "metapod", gridCol = 6, gridRow = 1, level = 8 }
    }
end

function get_combat_balance()
    return {
        playerDamageMult = 0.96,
        enemyDamageMult = 1.04,
        playerDamageTakenMult = 1.04,
        enemyDamageTakenMult = 0.98
    }
end

local transitioned = false

function on_update(dt)
    if transitioned then return end

    local units = world_list_units() or {}
    local anyEnemyAlive = false
    local anyPlayerAlive = false

    for i = 1, #units do
        local u = units[i]
        if u.side == "Enemy" then
            if u.alive or u.captureInProgress then
                anyEnemyAlive = true
            end
        elseif u.side == "Player" then
            if u.alive then
                anyPlayerAlive = true
            end
        end
    end

    if anyPlayerAlive and (not anyEnemyAlive) then
        transitioned = true
        emit("Route 3 cleared!")
        pop_state()
        push_state("scripts/states/adventure/route3_shop.lua")
    end
end
