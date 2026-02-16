-- scripts/states/route1.lua

local shared = dofile("scripts/states/shared/combat_route_shared.lua")
local mode = (get_game_mode and get_game_mode()) or "classic"
shared.install("route1", mode, _ENV)
