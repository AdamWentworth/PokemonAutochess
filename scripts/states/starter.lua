-- scripts/states/starter.lua

dofile("scripts/ui/starter_menu.lua")
hide_world = true
-- Presentation only: an editable Blender lab rendered through the UI asset path.
frontend_backdrop_image = "assets/ui/backdrops/oaks_lab.png"
frontend_backdrop_aspect = 1.6

function get_starter_cards()
    return {
        { name = "bulbasaur", cost = 0, type = "Starter" },
        { name = "charmander", cost = 0, type = "Starter" },
        { name = "squirtle",   cost = 0, type = "Starter" },
    }
end

function get_message()
    return "CHOOSE YOUR STARTER"
end

local function _do_spawn(pokemon)
    spawn_on_grid(pokemon, 3, 6, "Player", 5)  -- force starters to L5
end

-- If C++ expects snake_case:
function on_card_click(pokemon)
    _do_spawn(pokemon)
end

-- If C++ expects camelCase:
function onCardClick(pokemon)
    _do_spawn(pokemon)
end
