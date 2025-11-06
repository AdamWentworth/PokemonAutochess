-- scripts/systems/round_system.lua
-- Global-style functions so C++ can call them directly via sol2.

-- Timings (match old C++)
local planningDuration   = 30.0
local battleDuration     = 10.0
local resolutionDuration =  5.0

local current = "Planning"
local timer   = planningDuration

local function advance()
    local prev = current
    if     current == "Planning"   then current = "Battle";     timer = battleDuration
    elseif current == "Battle"     then current = "Resolution";  timer = resolutionDuration
    elseif current == "Resolution" then current = "Planning";    timer = planningDuration
    else  current = "Planning";    timer = planningDuration
    end
    -- C++ binding provided in LuaBindings.cpp
    emit_round_phase_changed(prev, current)
end

function rs_init()
    current = "Planning"
    timer   = planningDuration
end

function rs_update(dt)
    if dt > 0.25 then dt = 0.25 end
    timer = timer - dt
    if timer <= 0.0 then
        advance()
    end
end

function rs_get_phase()
    return current
end
