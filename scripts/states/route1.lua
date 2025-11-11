-- scripts/states/route1.lua

function get_message()
    return "Route 1 - Wild Pokémon Appeared!"
end

function get_enemies()
    return {
        { name = "pidgey",  gridCol = 2, gridRow = 1 },
        { name = "rattata", gridCol = 5, gridRow = 1 },
    }
end

-- You can also add on_enter/on_update/on_exit hooks if needed:
-- function on_enter() end
-- function on_update(dt) end
-- function on_exit() end
