-- scripts/states/route1.lua

function get_message()
    return "Route 1 - Wild Pokémon Appeared!"
end

function get_enemies()
    return {
        { name = "pidgey",  gridCol = 2, gridRow = 1, level = 2 },
        { name = "rattata", gridCol = 5, gridRow = 1, level = 3 },
    }
end

-- Intro encounter tuning: make starters win comfortably.
function get_combat_balance()
    return {
        playerDamageMult = 1.40,
        enemyDamageMult = 0.80,
        playerDamageTakenMult = 0.85,
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
        emit("Route 1 cleared!")
        pop_state()
        push_state("scripts/states/route1_shop.lua")
    end
end
