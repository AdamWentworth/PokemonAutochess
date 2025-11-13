-- scripts/systems/combat.lua

local COOLDOWN = 1.0
local timers = {}

local GAIN_ON_ATTACK = 12
local GAIN_ON_HIT    = 8

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
    world_set_energy(id, 0)
    emit("Charged", string.format("{\"unit\":%d}", id))
    return true
  end
  return false
end

function combat_init()
  timers = {}
  local units = world_list_units() or {}
  for i=1,#units do
    local u = units[i]
    world_set_energy(u.id, 0)
  end
  emit("Combat", "{\"init\":true}")
end

function combat_update(dt)
  dt = clamp(dt or 0.016, 0.0, 0.25)

  local units = world_list_units()
  if not units then return end

  for i=1,#units do
    local u = units[i]
    reset_if_missing(u.id)
    timers[u.id] = math.max(0.0, (timers[u.id] or 0.0) - dt)
  end

  for i=1,#units do
    local u = units[i]
    if u.alive and world_is_adjacent_to_enemy(u.id) then
      if timers[u.id] <= 0.0 then
        local targetId = find_adjacent_enemy(u.id)
        if targetId then
          local before = world_get_unit_snapshot(targetId)
          local rem = world_apply_damage(u.id, targetId, u.attack)
          timers[u.id] = COOLDOWN

          world_add_energy(u.id, GAIN_ON_ATTACK)
          world_add_energy(targetId, GAIN_ON_HIT)

          -- Log the hit
          emit("Attack", string.format("{\"attacker\":%d,\"target\":%d,\"dmg\":%d,\"hp\":%d}", u.id, targetId, u.attack, rem))

          -- KO line
          if rem == 0 then
            emit("KO", string.format("{\"attacker\":%d,\"target\":%d}", u.id, targetId))
          end

          -- Auto-casts
          try_fire_charged(u.id)
          try_fire_charged(targetId)
        end
      end
    end
  end

  for i=1,#units do
    local u = units[i]
    if u.alive then world_face_enemy(u.id) end
  end
end
