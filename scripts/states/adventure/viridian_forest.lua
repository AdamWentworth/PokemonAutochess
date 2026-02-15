-- scripts/states/adventure/viridian_forest.lua

function get_message()
    return "Viridian Forest - The bugs are swarming!"
end

function get_enemies()
    return {
        { name = "metapod",  gridCol = 1, gridRow = 1, level = 7 },
        { name = "kakuna",   gridCol = 2, gridRow = 1, level = 7 },
        { name = "pikachu",  gridCol = 4, gridRow = 1, level = 6 },
        { name = "caterpie", gridCol = 5, gridRow = 1, level = 6 },
        { name = "weedle",   gridCol = 6, gridRow = 1, level = 6 }
    }
end

function get_combat_balance()
    return {
        playerDamageMult = 1.00,
        enemyDamageMult = 1.00,
        playerDamageTakenMult = 1.00,
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
        emit("Viridian Forest cleared!")
        pop_state()
        push_state("scripts/states/adventure/route3_shop.lua")
    end
end
