-- scripts/ui/starter_menu.lua

-- Cards shown on the starter screen
function get_starter_cards()
    return {
        { name = "bulbasaur", cost = 0, type = "Starter" },
        { name = "charmander", cost = 0, type = "Starter" },
        { name = "squirtle",   cost = 0, type = "Starter" },
    }
end

-- Map keys to starters (lets us change bindings without C++)
function handle_starter_key(key)
    if key == "1" then return "bulbasaur" end
    if key == "2" then return "charmander" end
    if key == "3" then return "squirtle"   end
    return nil
end
