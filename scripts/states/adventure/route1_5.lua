-- scripts/states/adventure/route1_5.lua

function get_message()
    return "Route 1.5 - Another wave!"
end

function get_enemies()
    return {
        { name = "pidgey",  gridCol = 1, gridRow = 1, level = 3 },
        { name = "rattata", gridCol = 4, gridRow = 1, level = 4 },
        { name = "pidgey",  gridCol = 6, gridRow = 1, level = 3 },
    }
end

function get_combat_balance()
    return {
        playerDamageMult = 1.20,
        enemyDamageMult = 0.90,
        playerDamageTakenMult = 0.90,
        enemyDamageTakenMult = 1.00
    }
end
