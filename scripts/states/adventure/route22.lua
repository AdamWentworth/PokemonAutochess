-- scripts/states/adventure/route22.lua

function get_message()
    return "Route 22 - New challengers!"
end

function get_enemies()
    return {
        { name = "nidoran-f", gridCol = 1, gridRow = 1, level = 4 },
        { name = "spearow",   gridCol = 3, gridRow = 1, level = 5 },
        { name = "nidoran-m", gridCol = 5, gridRow = 1, level = 5 },
        { name = "mankey",    gridCol = 6, gridRow = 1, level = 6 },
    }
end

function get_combat_balance()
    return {
        playerDamageMult = 1.10,
        enemyDamageMult = 1.00,
        playerDamageTakenMult = 0.95,
        enemyDamageTakenMult = 1.00
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
        emit("Route 22 cleared!")
        pop_state()
        push_state("scripts/states/adventure/route22_shop.lua")
    end
end
