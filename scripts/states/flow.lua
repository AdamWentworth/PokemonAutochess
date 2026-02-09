-- scripts/states/flow.lua

-- Decide which combat script to load after placement
function next_route_after_placement(starter_name)
    local mode = get_game_mode and get_game_mode() or "classic"
    if mode == "adventure" then
        return "scripts/states/adventure/route1.lua"
    end

    -- classic default
    return "scripts/states/classic/route1.lua"
end
