-- scripts/states/flow.lua

-- Decide which combat script to load after placement
function next_route_after_placement(starter_name)
    -- branch here later if you like (by starter, difficulty, etc.)
    return "scripts/states/route1.lua"
end
