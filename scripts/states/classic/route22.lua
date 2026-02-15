-- scripts/states/classic/route22.lua

dofile("scripts/states/classic/round_economy.lua")

function get_message()
    return "Route 22 - New challengers!"
end

function get_enemies()
    return {
        { name = "nidoran-f", gridCol = 1, gridRow = 1, level = 4 },
        { name = "spearow",   gridCol = 3, gridRow = 1, level = 5 },
        { name = "nidoran-m", gridCol = 5, gridRow = 1, level = 4 },
        { name = "mankey",    gridCol = 6, gridRow = 1, level = 4 },
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
    if classic_try_finish_round("scripts/states/classic/route2_shop.lua") then
        transitioned = true
    end
end
