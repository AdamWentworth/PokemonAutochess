-- scripts/systems/combat.lua
-- Pokémon-like battle log lines for basic attacks.
-- Emits ONLY plain text (no bracketed/JSON payloads).

local COOLDOWN = 1.0
local timers = {}

-- Energy rules
local GAIN_ON_ATTACK = 12
local GAIN_ON_HIT    = 8

-- Move naming for fast/basic
local BASIC_MOVE_NAME = "Tackle"

-- RNG helpers
local function randf() return math.random() end

-- Tunables
local MISS_CHANCE = 0.10
local CRIT_CHANCE = 0.125
local CRIT_MULT   = 1.5

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
local function reset_if_missing(id) if timers[id] == nil then timers[id] = 0.0 end end

local function get_name(unit_id)
  local u = world_get_unit_snapshot(unit_id)
  if u and u.name and #u.name > 0 then
    local first = string.upper(string.sub(u.name, 1, 1))
    local rest  = string.sub(u.name, 2)
    return first .. rest
  end
  return "Unknown"
end

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

-- Placeholder for future type system
local function effectiveness(user_id, target_id)
  return "neutral"
end

local function maybe_emit_effectiveness(tag)
  if tag == "super" then
    emit("It's super effective!")
  elseif tag == "not_very" then
    emit("It's not very effective…")
  elseif tag == "immune" then
    emit("It doesn’t affect the target…")
  end
end

local function try_fire_charged(id)
  local cur = world_get_energy(id)
  local cap = world_get_max_energy(id)
  if cur >= cap and cap > 0 then
    world_set_energy(id, 0)
    -- No structured/event emits; keep silent or add plain text if desired
    return true
  end
  return false
end

function combat_init()
  timers = {}
  local units = world_list_units() or {}
  for i = 1, #units do
    local u = units[i]
    world_set_energy(u.id, 0)
  end
  -- No "Combat {init:true}" payload emit
end

function combat_update(dt)
  dt = clamp(dt or 0.016, 0.0, 0.25)

  local units = world_list_units()
  if not units then return end

  for i = 1, #units do
    local u = units[i]
    reset_if_missing(u.id)
    timers[u.id] = math.max(0.0, (timers[u.id] or 0.0) - dt)
  end

  for i = 1, #units do
    local u = units[i]
    if u.alive and world_is_adjacent_to_enemy(u.id) then
      if timers[u.id] <= 0.0 then
        local targetId = find_adjacent_enemy(u.id)
        if targetId then
          local userName   = get_name(u.id)
          local targetName = get_name(targetId)

          -- Move announce
          emit(string.format("%s used %s!", userName, BASIC_MOVE_NAME))

          -- Miss
          if randf() < MISS_CHANCE then
            emit(string.format("%s missed!", BASIC_MOVE_NAME))
            world_add_energy(u.id, GAIN_ON_ATTACK)
            timers[u.id] = COOLDOWN
          else
            -- Hit
            local dmg = u.attack
            local isCrit = (randf() < CRIT_CHANCE)
            if isCrit then
              dmg = math.floor(dmg * CRIT_MULT + 0.5)
            end

            local eff = effectiveness(u.id, targetId)
            if eff == "immune" then
              emit(string.format("It doesn’t affect %s…", targetName))
              world_add_energy(u.id, GAIN_ON_ATTACK)
              timers[u.id] = COOLDOWN
            else
              local rem = world_apply_damage(u.id, targetId, dmg)
              timers[u.id] = COOLDOWN

              world_add_energy(u.id, GAIN_ON_ATTACK)
              world_add_energy(targetId, GAIN_ON_HIT)

              if isCrit then emit("A critical hit!") end
              maybe_emit_effectiveness(eff)

              if rem == 0 then
                emit(string.format("%s fainted!", targetName))
              end

              try_fire_charged(u.id)
              try_fire_charged(targetId)
            end
          end
        end
      end
    end
  end

  for i = 1, #units do
    local u = units[i]
    if u.alive then world_face_enemy(u.id) end
  end
end
