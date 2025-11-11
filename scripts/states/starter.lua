-- scripts/states/starter.lua

-- Reuse existing menu helper for key mapping
dofile("scripts/ui/starter_menu.lua")

-- Cards to show
function get_starter_cards()
    return {
        { name = "bulbasaur", cost = 0, type = "Starter" },
        { name = "charmander", cost = 0, type = "Starter" },
        { name = "squirtle",   cost = 0, type = "Starter" },
    }
end

-- Optional: title text
function get_message()
    return "CHOOSE YOUR STARTER"
end

-- Called by C++ when a card is clicked or a bound key is pressed
function on_card_click(pokemon)
    -- Place the chosen starter on the player's back rank, center-ish
    -- Board is 8x8. Put starter at col 3, row 6 (player side)
    spawn_on_grid(pokemon, 3, 6, "Player")

    -- Go straight to Route 1 combat (Lua-driven)
    push_state("scripts/states/route1.lua")
end
