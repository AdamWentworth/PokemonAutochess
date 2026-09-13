-- scripts/states/flow.lua

-- Starter placement and its first encounter share this arena in both modes.
function next_route_after_placement(starter_name)
    return "scripts/states/route1_flat_experiment.lua"
end
