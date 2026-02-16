-- scripts/states/route22_shop.lua

local shared = dofile("scripts/states/shared/shop_state_shared.lua")
local mode = (get_game_mode and get_game_mode()) or "classic"
shared.install("route22_shop", mode, _ENV)
