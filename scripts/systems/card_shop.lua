-- scripts/systems/card_shop.lua
-- Lua owns odds/rules; C++ (ShopSystem) owns UI & gold.

-- odds per player level -> weights for (tier1..tier5)
local ODDS = {
  [1] = {80, 20, 0, 0, 0},
  [2] = {70, 25, 5, 0, 0},
  [3] = {55, 35, 10, 0, 0},
  [4] = {45, 35, 15, 5, 0},
  [5] = {30, 40, 20, 10, 0},
}

-- Simple Pokémon pools mapped by tier
local POOLS = {
  [1] = {"pidgey", "rattata"},
  [2] = {"bulbasaur", "charmander", "squirtle"},
  [3] = {"pidgey", "bulbasaur", "charmander", "squirtle"},
  [4] = {"bulbasaur", "charmander", "squirtle"},
  [5] = {"bulbasaur", "charmander", "squirtle"},
}

local BASE_COSTS = {
  pidgey = 2,
  rattata = 2,
  bulbasaur = 3,
  charmander = 3,
  squirtle = 3,
}

local function pick_weighted(weights)
  local sum = 0
  for i=1,#weights do sum = sum + weights[i] end
  if sum <= 0 then return 1 end
  local r = randf() * sum
  local acc = 0
  for i=1,#weights do
    acc = acc + weights[i]
    if r < acc then return i end
  end
  return #weights
end

local function pick_from(pool)
  if #pool == 0 then return "rattata" end
  local idx = randi(#pool)
  if idx < 1 then idx = 1 end
  if idx > #pool then idx = #pool end
  return pool[idx]
end

function shop_init()
  -- no-op; keep for symmetry
end

function on_round_start(round_idx)
  emit("RoundStart", "{\"round\":"..tostring(round_idx).."}")
end

function on_level_changed(player_id, new_level)
  emit("LevelChanged", "{\"player\":"..tostring(player_id)..",\"level\":"..tostring(new_level).."}")
end

-- price_for(cardIdOrName, base_cost, player_id) -> number
-- (implemented host-side; Lua may call it as needed)
-- get_gold(player_id), get_level(player_id) -> numbers (host-side)

-- returns table:
-- {
--   slots = {
--     { name="bulbasaur", cost=3, type="Shop" },
--     ...
--   },
--   reroll_cost = N,
--   free_reroll = bool
-- }
function shop_roll(player_id)
  local lvl = get_level(player_id)
  local weights = ODDS[lvl] or ODDS[1]
  local tier = pick_weighted(weights)

  local out = { slots = {}, reroll_cost = 2, free_reroll = false }
  for i=1,5 do
    local p = POOLS[tier] or {}
    local name = pick_from(p)
    local base = BASE_COSTS[name] or 3
    local price = price_for(name, base, player_id)
    table.insert(out.slots, { name = name, cost = price, type = "Shop" })
  end

  emit("ShopRolledLua", "{\"player\":"..tostring(player_id).."}")
  return out
end

-- can_buy(player_id, slot_index_1based) -> bool
function can_buy(player_id, slot_index)
  local g = get_gold(player_id)
  -- simple rule: must have >= cheapest current roll price
  -- (host will recompute the precise price)
  return g >= 2
end
