-- scripts/systems/combat.lua

-- Fast-attack params (existing behavior)
local COOLDOWN = 1.0
local timers = {} -- id -> time left

-- Energy system (charged attacks)
local GAIN_ON_ATTACK = 12      -- energy gained when this unit does a fast attack
local GAIN_ON_HIT    = 8       -- energy gained when this unit takes damage
-- You can later make these per-Pokémon (by name) if needed.

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
local function reset_if_missing(id) if timers[id] == nil then timers[id] = 0.0 end end

local function find_adjacent_enemy(id)
  local enemies = world_enemies_adjacent(id)
  if enemies and #enemies > 0 then
    local bestId, bestHP, bestTie = nil, math.huge, math.huge
    for _,eid in ipairs(enemies) do
      local e = world_get_unit_snapshot(eid)
      if e and e.alive then
        if e.hp < bestHP or (e.hp == bestHP and e.id < bestTie) then
          bestHP = e.hp; bestId = e.id; bestTie = e.id
        end
      end
    end
    return bestId
  end
  return nil
end

local function try_fire_charged(id)
  local cur = world_get_energy(id)
  local cap = world_get_max_energy(id)
  if cur >= cap and cap > 0 then
    -- Placeholder: actual effect TBD. For now, just emit and reset.
    -- You can expand this to apply damage/aoe/buffs later.
    -- Example stub: print or emit an event.
    -- print(("Charged attack! unit %d"):format(id))
    world_set_energy(id, 0)
    return true
  end
  return false
end

function combat_init()
  timers = {}
  -- Optional: zero out energy at combat start (engine defaults to 0 already)
  local units = world_list_units() or {}
  for i=1,#units do
    local u = units[i]
    world_set_energy(u.id, 0)
  end
end

function combat_update(dt)
  dt = clamp(dt or 0.016, 0.0, 0.25)

  local units = world_list_units()
  if not units then return end

  -- Tick cooldowns
  for i=1,#units do
    local u = units[i]
    reset_if_missing(u.id)
    timers[u.id] = math.max(0.0, (timers[u.id] or 0.0) - dt)
  end

  -- Resolve fast attacks + energy on attack/hit
  for i=1,#units do
    local u = units[i]
    if u.alive and world_is_adjacent_to_enemy(u.id) then
      if timers[u.id] <= 0.0 then
        local targetId = find_adjacent_enemy(u.id)
        if targetId then
          -- Fast attack
          world_apply_damage(u.id, targetId, u.attack)
          timers[u.id] = COOLDOWN

          -- Charge: attacker gains on attack
          world_add_energy(u.id, GAIN_ON_ATTACK)

          -- Charge: target gains on being hit
          world_add_energy(targetId, GAIN_ON_HIT)

          -- Auto-cast if full (attacker)
          try_fire_charged(u.id)
          -- Optional: also allow the target to auto-cast if the hit filled its bar
          try_fire_charged(targetId)
        end
      end
    end
  end

  -- Orientation polish
  for i=1,#units do
    local u = units[i]
    if u.alive then world_face_enemy(u.id) end
  end
end
