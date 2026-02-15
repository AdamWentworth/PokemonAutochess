-- scripts/states/classic/route2.lua

dofile("scripts/states/classic/round_economy.lua")

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
    if classic_try_finish_round("scripts/states/classic/viridian_forest_shop.lua") then
        transitioned = true
    end
end
