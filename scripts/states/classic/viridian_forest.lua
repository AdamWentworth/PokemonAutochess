-- scripts/states/classic/viridian_forest.lua

dofile("scripts/states/classic/round_economy.lua")

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
    if classic_try_finish_round("scripts/states/classic/route3_shop.lua") then
        transitioned = true
    end
end
