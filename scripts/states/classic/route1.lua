-- scripts/states/classic/route1.lua

dofile("scripts/states/classic/round_economy.lua")

function get_message()
    return "Route 1 - Wild Pokemon Appeared!"
end

function get_enemies()
    return {
        { name = "pidgey",  gridCol = 2, gridRow = 1, level = 2 },
        { name = "rattata", gridCol = 5, gridRow = 1, level = 3 },
    }
end

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
    if classic_try_finish_round("scripts/states/classic/route1_shop.lua") then
        transitioned = true
    end
end
