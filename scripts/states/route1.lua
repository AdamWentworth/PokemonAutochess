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

-- You can also add on_enter/on_update/on_exit hooks if needed:
-- function on_enter() end
-- function on_update(dt) end
-- function on_exit() end
