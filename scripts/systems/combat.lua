local timers = {}
local rng = function() return math.random() end

local MISS_CHANCE = 0.10
local CRIT_CHANCE = 0.125
local CRIT_MULT   = 1.5

-- small helpers
local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
local function reset_if_missing(id) if timers[id] == nil then timers[id] = 0.0 end end

-- Timing/speed tuning is centralized here (optional; defaults below if missing).
local TUNING = {
  FAST_CD_MULT = 2.0,
  CHARGED_CD_MULT = 2.0,
  MIN_FAST_REQUEST_SEC = 0.85,
  MIN_CHARGED_REQUEST_SEC = 0.85,
  SPEED_BASELINE = 1.0,
  SPEED_MIN = 0.35,
  SPEED_MAX = 3.0
}

do
  local ok, t = pcall(dofile, "scripts/config/combat_tuning.lua")
  if ok and type(t) == "table" then
    for k,v in pairs(t) do TUNING[k] = v end
  end
end

-- DEBUG: focused tracing for Bulbasaur -> vine_whip. Uses emit("[TAG]", payload) which prints to terminal only.
local function is_vinewhip_debug(unit, moveName)
  if not unit or not moveName then return false end
  return (unit.name == "bulbasaur" and moveName == "vine_whip")
end

local function vwlog(tag, payload)
  emit(tag, payload)
end

local FAST_CD_MULT    = TUNING.FAST_CD_MULT
local CHARGED_CD_MULT = TUNING.CHARGED_CD_MULT
local MIN_FAST_REQUEST_SEC    = TUNING.MIN_FAST_REQUEST_SEC
local MIN_CHARGED_REQUEST_SEC = TUNING.MIN_CHARGED_REQUEST_SEC

-- Optional per-move animation/cadence multiplier (1.25 = 25% faster)
local MOVE_SPEED_MULT = {
  vine_whip = 1.25,
}

local function move_speed_mult(move_name)
  if not move_name or move_name == "" then return 1.0 end
  local m = MOVE_SPEED_MULT[move_name]
  if type(m) == "number" and m > 0.0 then return m end
  return 1.0
end

-- Shared "speed" stat used for both movement and attack cadence.
local function unit_speed_factor(unit_id)
  local s = nil

  if type(world_get_unit_speed) == "function" then
    local v = world_get_unit_speed(unit_id)
    if type(v) == "number" then s = v end
  end

  if s == nil and type(world_get_unit_snapshot) == "function" then
    local u = world_get_unit_snapshot(unit_id)
    if u then
      if type(u.speed) == "number" then s = u.speed end
      if s == nil and type(u.movementSpeed) == "number" then s = u.movementSpeed end
    end
  end

  if type(s) ~= "number" then s = TUNING.SPEED_BASELINE end

  local denom = (TUNING.SPEED_BASELINE or 1.0)
  if denom == 0 then denom = 1.0 end
  local f = s / denom
  return clamp(f, TUNING.SPEED_MIN, TUNING.SPEED_MAX)
end

-- Query engine-side per-move override if available.
local function min_request_sec(attacker_id, move_name, kind, base)
  local v = base
  if type(world_attack_min_request_sec) == "function" and move_name and move_name ~= "" then
    local o = world_attack_min_request_sec(attacker_id, move_name, kind)
    if type(o) == "number" and o > v then v = o end
  end
  return v
end

-- attack gating helper (prevents spending cooldown/energy while airborne/landing)
local function can_attack_now(id)
  if type(world_can_attack) == "function" then
    return world_can_attack(id)
  end
  return true
end

-- stronger gating: only start a *new* attack cycle when engine is ready.
-- (prevents charged from preempting and avoids loop restarts mid-cycle)
local function can_start_attack_now(id)
  if type(world_attack_ready) == "function" then
    return world_attack_ready(id)
  end
  return can_attack_now(id)
end

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

local function effectiveness(user_id, target_id)
  return "neutral"
end

local function maybe_emit_effectiveness(tag)
  if tag == "super" then emit("It's super effective!")
  elseif tag == "not_very" then emit("It's not very effective…")
  elseif tag == "immune" then emit("It doesn’t affect the target…")
  end
end

-- Combat engagement tracking for animation state
local engaged_state = {}

local function wants_to_move(id)
  if type(world_has_planned_move) == "function" then
    return world_has_planned_move(id)
  end
  if type(world_is_moving) == "function" then
    return world_is_moving(id)
  end
  return false
end

local function set_engaged(id, engaged)
  local prev = engaged_state[id] == true
  if prev == engaged then return end
  engaged_state[id] = engaged

  if type(world_set_in_combat) == "function" then
    world_set_in_combat(id, engaged)
  end

  if prev and (not engaged) and wants_to_move(id) then
    if type(world_request_combat_end) == "function" then
      world_request_combat_end(id)
    end
  end
end

-- NEW: charged does not preempt. When gauge becomes full, mark "charged_pending";
-- next attack cycle started after the current one ends will be charged.
local charged_pending = {}

local function mark_charged_pending_if_ready(id)
  local name = unit_charged_move(id)
  if not name or name == "" then return end
  local m = move_get(name)
  local cur = world_get_energy(id)
  local cap = world_get_max_energy(id)
  -- Moves DB may store energyCost as 0 when unspecified.
  -- Treat missing/zero as "use maxEnergy".
  local need = m.energyCost
  if type(need) ~= "number" or need <= 0 then need = cap end
  if type(need) ~= "number" or need <= 0 then return end

  if cur >= need then
    charged_pending[id] = true
  end
end

local function fire_charged(id, tgt)
  local name = unit_charged_move(id)
  if not name or name == "" then return false end
  local m = move_get(name)
  local cur = world_get_energy(id)
  local cap = world_get_max_energy(id)
  local need = m.energyCost
  if type(need) ~= "number" or need <= 0 then need = cap end
  if type(need) ~= "number" or need <= 0 then return false end
  if cur < need then return false end
  if not tgt then return false end
  if not can_start_attack_now(id) then return false end

  local spd = unit_speed_factor(id)
  local cd = math.max(0.05, (m.cooldownSec or 0.8))
  cd = (cd * CHARGED_CD_MULT) / spd
  cd = math.max(cd, min_request_sec(id, name, "charged", MIN_CHARGED_REQUEST_SEC) / spd)

  world_set_energy(id, cur - need)
  emit(string.format("%s used %s!", get_name(id), string.gsub(name, "_", " ")))

  local tSnap = world_get_unit_snapshot(tgt)
  local hp_before = tSnap and tSnap.hp or 0

  local dmg = m.power or 0
  if rng() < CRIT_CHANCE then
    dmg = math.floor(dmg * CRIT_MULT + 0.5)
    emit("A critical hit!")
  end

  local rem = world_apply_damage(id, tgt, dmg, cd, name, "charged")
  local eff = effectiveness(id, tgt)
  maybe_emit_effectiveness(eff)
  if rem == 0 then emit(string.format("%s fainted!", get_name(tgt))) end

  timers[id] = cd
  charged_pending[id] = false
  return true
end

function combat_init()
  timers = {}
  engaged_state = {}
  charged_pending = {}

  local units = world_list_units() or {}
  for i = 1, #units do
    local u = units[i]
    world_set_energy(u.id, 0)
    engaged_state[u.id] = false
    charged_pending[u.id] = false
    if type(world_set_in_combat) == "function" then
      world_set_in_combat(u.id, false)
    end
  end
end

function combat_update(dt)
  dt = clamp(dt or 0.016, 0.0, 0.25)

  local units = world_list_units()
  if not units then return end

  -- Update per-unit cooldown timers
  for i = 1, #units do
    local u = units[i]
    reset_if_missing(u.id)
    timers[u.id] = math.max(0.0, (timers[u.id] or 0.0) - dt)
    if charged_pending[u.id] == nil then charged_pending[u.id] = false end
  end

  -- Update combat engagement state FIRST (animation driver)
  for i = 1, #units do
    local u = units[i]
    if u.alive then
      local adjacent = (type(world_is_adjacent_to_enemy) == "function") and world_is_adjacent_to_enemy(u.id) or false
      set_engaged(u.id, adjacent)
    else
      set_engaged(u.id, false)
    end
  end

  -- Combat resolution
  for i = 1, #units do
    local u = units[i]
    if u.alive and world_is_adjacent_to_enemy(u.id) then
      local tgt = find_adjacent_enemy(u.id)
      if tgt then
        -- Update pending-charged flag as soon as gauge fills.
        mark_charged_pending_if_ready(u.id)

        -- If we have a pending charged, do NOT start any more fast cycles.
        -- The next cycle we are allowed to start will be charged.
        if charged_pending[u.id] and timers[u.id] <= 0.0 then
          if fire_charged(u.id, tgt) then
            goto continue_unit
          end
        end

        -- Fast move (only when we are not waiting to fire charged)
        if (not charged_pending[u.id]) and timers[u.id] <= 0.0 then
          local fastName = unit_fast_move(u.id)
          local m = move_get(fastName)

          local spd = unit_speed_factor(u.id)

          if is_vinewhip_debug(u, fastName) then
            vwlog("[VW_FAST/select]", string.format(
              "attackerId=%d name=%s move=%s base_cd=%.3f energyGain=%s power=%s FAST_CD_MULT=%.3f SPEED_BASELINE=%.3f spdFactor=%.3f timerBefore=%.3f pendingCharged=%s",
              u.id, u.name, fastName,
              (m.cooldownSec or 0.0),
              tostring(m.energyGain), tostring(m.power),
              FAST_CD_MULT, (TUNING.SPEED_BASELINE or 1.0), spd, (timers[u.id] or -1.0), tostring(charged_pending[u.id])
            ))
          end

          local cd = math.max(0.05, (m.cooldownSec or 0.5))
          local mm = move_speed_mult(fastName)
          cd = (cd * FAST_CD_MULT) / (spd * mm)
          cd = math.max(cd, min_request_sec(u.id, fastName, "fast", MIN_FAST_REQUEST_SEC) / (spd * mm))

          -- Only start a new cycle when the engine is ready.
          if not can_start_attack_now(u.id) then
            if is_vinewhip_debug(u, fastName) then
              vwlog("[VW_FAST/can_start_attack_now]", "false -> skip (no cosmetic request)")
            end
            goto continue_unit
          end

          timers[u.id] = cd
          if is_vinewhip_debug(u, fastName) then
            vwlog("[VW_FAST/fire]", string.format("start_fast_attack cd=%.3f timerSet=%.3f targetId=%d", cd, timers[u.id], tgt))
          end

          emit(string.format("%s used %s!", get_name(u.id), string.gsub(fastName, "_", " ")))

          if rng() < MISS_CHANCE then
            emit("It missed!")
            world_apply_damage(u.id, tgt, 0, cd, fastName, "fast")
          else
            local dmg = m.power or 0
            if rng() < CRIT_CHANCE then
              dmg = math.floor(dmg * CRIT_MULT + 0.5)
              emit("A critical hit!")
            end
            local rem = world_apply_damage(u.id, tgt, dmg, cd, fastName, "fast")
            world_add_energy(tgt, 8)
            local eff = effectiveness(u.id, tgt); maybe_emit_effectiveness(eff)
            if rem == 0 then emit(string.format("%s fainted!", get_name(tgt))) end
          end

          world_add_energy(u.id, m.energyGain or 0)
        end
      end
    end
    ::continue_unit::
  end

  -- Passive face nearest enemy
  for i = 1, #units do
    local u = units[i]
    if u.alive then world_face_enemy(u.id) end
  end
end
