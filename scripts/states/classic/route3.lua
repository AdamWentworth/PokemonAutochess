-- scripts/states/classic/route3.lua

dofile("scripts/states/classic/round_economy.lua")

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
    if classic_try_finish_round("scripts/states/classic/route3_shop.lua") then
        transitioned = true
    end
end
