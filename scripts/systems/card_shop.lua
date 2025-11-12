-- card_shop.lua
-- Lua owns odds/rules; C++ is authoritative for state/RNG/pricing.

-- odds per player level -> weights for (tier1..tier5)
local ODDS = {
  [1] = {80, 20, 0, 0, 0},
  [2] = {70, 25, 5, 0, 0},
  [3] = {55, 35, 10, 0, 0},
  [4] = {45, 35, 15, 5, 0},
  [5] = {30, 40, 20, 10, 0},
}

-- demo card pools per tier (replace with your unit ids)
local POOLS = {
  [1] = {101,102,103,104,105},
  [2] = {201,202,203,204,205},
  [3] = {301,302,303,304,305},
  [4] = {401,402,403,404,405},
  [5] = {501,502,503,504,505},
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
  if #pool == 0 then return -1 end
  local idx = randi(#pool) + 1
  return pool[idx]
end

function shop_init()
  -- placeholder: precompute anything here
end

function on_round_start(round_idx)
  emit("RoundStart", "{\"round\":"..tostring(round_idx).."}")
end

function on_level_changed(player_id, new_level)
  emit("LevelChanged", "{\"player\":"..tostring(player_id)..",\"level\":"..tostring(new_level).."}")
end

-- returns table: { slots = {id,id,id,id,id}, reroll_cost = N, free_reroll = bool }
function shop_roll(player_id)
  local lvl = get_level(player_id)
  local weights = ODDS[lvl] or ODDS[1]
  local tier = pick_weighted(weights)

  local slots = {}
  for i=1,5 do
    local p = POOLS[tier] or {}
    slots[i] = pick_from(p)
  end

  local t = {
    slots = slots,
    reroll_cost = 2,
    free_reroll = false
  }
  emit("ShopRolledLua", "{\"player\":"..tostring(player_id).."}")
  return t
end

-- can_buy(player_id, slot_index_1based) -> bool, reason?
function can_buy(player_id, slot_index)
  local lvl = get_level(player_id)
  if lvl < 1 then return false, "invalid level" end

  -- obtain a quoted price from C++ (authoritative)
  local base_cost = 3
  -- In a real version, you'd read the current slot's card id and pass here.
  local quoted_price = price_for(-1, base_cost, player_id)

  if get_gold(player_id) < quoted_price then
    return false, "not enough gold"
  end
  return true
end
