-- scripts/states/viridian_forest.lua

local shared = dofile("scripts/states/shared/combat_route_shared.lua")
local mode = (get_game_mode and get_game_mode()) or "classic"
shared.install("viridian_forest", mode, _ENV)
