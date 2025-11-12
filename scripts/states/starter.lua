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
  spawn_on_grid(pokemon, 3, 6, "Player", 5)  -- force starters to L5
end
