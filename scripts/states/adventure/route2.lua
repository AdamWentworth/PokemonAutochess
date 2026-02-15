-- scripts/states/adventure/route2.lua

function get_message()
    return "Route 2 - Edge of Viridian Forest!"
end

function get_enemies()
    return {
        { name = "caterpie", gridCol = 1, gridRow = 1, level = 5 },
        { name = "weedle",   gridCol = 2, gridRow = 1, level = 5 },
        { name = "pidgey",   gridCol = 4, gridRow = 1, level = 6 },
        { name = "caterpie", gridCol = 5, gridRow = 1, level = 5 },
        { name = "weedle",   gridCol = 6, gridRow = 1, level = 5 }
    }
end

function get_combat_balance()
    return {
        playerDamageMult = 1.05,
        enemyDamageMult = 1.00,
        playerDamageTakenMult = 0.97,
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
        emit("Route 2 cleared!")
        pop_state()
        push_state("scripts/states/adventure/viridian_forest_shop.lua")
    end
end
