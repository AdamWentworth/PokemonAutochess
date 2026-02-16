-- scripts/states/flow.lua

-- Shared combat route entry; mode-specific behavior is resolved inside scripts.
function next_route_after_placement(starter_name)
    return "scripts/states/route1.lua"
end
