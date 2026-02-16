-- scripts/states/route3.lua

local shared = dofile("scripts/states/shared/combat_route_shared.lua")
local mode = (get_game_mode and get_game_mode()) or "classic"
shared.install("route3", mode, _ENV)
