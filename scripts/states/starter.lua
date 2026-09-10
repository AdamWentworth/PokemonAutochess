-- scripts/states/starter.lua

dofile("scripts/ui/starter_menu.lua")
hide_world = true
-- Presentation only: an editable Blender lab rendered through the UI asset path.
frontend_backdrop_image = "assets/ui/backdrops/oaks_lab.png"
frontend_backdrop_aspect = 1.6
-- Image-space focus on the original right-hand starter table. The camera settles
-- before both UI bands fade in; mouse/number-key selection unlocks after the fade.
frontend_intro = {
    hold_seconds = 0.65,
    move_seconds = 1.90,
    settle_seconds = 0.15,
    fade_seconds = 0.65,
    focus_u = 0.706,
    focus_v = 0.575,
    zoom = 2.15,
}

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
