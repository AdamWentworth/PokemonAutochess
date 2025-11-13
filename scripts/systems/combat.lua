-- scripts/systems/combat.lua

local timers = {}
local rng = function() return math.random() end

local MISS_CHANCE = 0.10
local CRIT_CHANCE = 0.125
local CRIT_MULT   = 1.5

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
local function reset_if_missing(id) if timers[id] == nil then timers[id] = 0.0 end end

-- NEW: small JSON stringifier for primitives/flat tables
local function jstr(v)
  local t = type(v)
  if t == "string" then
    -- escape backslash and quotes; keep it simple for debug
    v = v:gsub("\\","\\\\"):gsub("\"","\\\"")
    return "\"" .. v .. "\""
  elseif t == "boolean" or t == "number" then
    return tostring(v)
  elseif v == nil then
    return "null"
  else
    return "\"" .. tostring(v) .. "\"" -- fallback
  end
end

local function jobj(tbl)
  local first = true
  local out = "{"
  for k,v in pairs(tbl) do
    if not first then out = out .. "," end
    first = false
    out = out .. "\"" .. k .. "\":" .. jstr(v)
  end
  return out .. "}"
end

-- NEW: emit a single structured terminal line
local function emit_struct(tag, fields)
  emit(tag, jobj(fields))
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

-- NEW: shared per-hit structured log (fast or charged)
local function log_hit(kind, attackerId, moveName, targetId, params)
  local a = world_get_unit_snapshot(attackerId) or {}
  local t = world_get_unit_snapshot(targetId) or {}
  local fields = {
    kind       = kind,                         -- "fast" | "charged"
    att_id     = attackerId,
    att_name   = a.name or "Unknown",
    move       = moveName,
    tgt_id     = targetId,
    tgt_name   = t.name or "Unknown",
    miss       = params.miss or false,
    crit       = params.crit or false,
    dmg        = params.dmg or 0,
    hp_before  = params.hp_before,
    hp_after   = params.hp_after,
    e_att_bef  = params.e_att_bef,
    e_att_aft  = params.e_att_aft,
    e_tgt_bef  = params.e_tgt_bef,
    e_tgt_aft  = params.e_tgt_aft
  }
  emit_struct("[COMBAT]", fields)
end

local function use_charged_if_ready(id)
  local name = unit_charged_move(id)
  if not name or name == "" then return false end
  local m = move_get(name)
  local cur = world_get_energy(id)
  local cap = world_get_max_energy(id)
  local need = (m.energyCost or cap)
  if cur >= need then
    local tgt = find_adjacent_enemy(id)
    -- energy bookkeeping (before)
    local e_att_bef = cur
    local e_tgt_bef = (tgt and world_get_energy(tgt)) or 0

    world_set_energy(id, cur - need)
    emit(string.format("%s used %s!", get_name(id), string.gsub(name, "_", " ")))

    if tgt then
      local tSnap = world_get_unit_snapshot(tgt)
      local hp_before = tSnap and tSnap.hp or 0

      local dmg = m.power or 0
      local crit = false
      if rng() < CRIT_CHANCE then
        dmg = math.floor(dmg * CRIT_MULT + 0.5)
        crit = true
        emit("A critical hit!")
      end

      local rem = world_apply_damage(id, tgt, dmg)

      local eff = effectiveness(id, tgt)
      maybe_emit_effectiveness(eff)

      local e_att_aft = world_get_energy(id)
      local e_tgt_aft = world_get_energy(tgt) -- defenders don’t gain energy from charged here

      -- structured line
      log_hit("charged", id, name, tgt, {
        miss=false, crit=crit, dmg=dmg,
        hp_before=hp_before, hp_after=rem,
        e_att_bef=e_att_bef, e_att_aft=e_att_aft,
        e_tgt_bef=e_tgt_bef, e_tgt_aft=e_tgt_aft
      })

      if rem == 0 then emit(string.format("%s fainted!", get_name(tgt))) end
    else
      -- No target (still log the spend)
      log_hit("charged", id, name, -1, {
        miss=false, crit=false, dmg=0,
        hp_before=nil, hp_after=nil,
        e_att_bef=e_att_bef, e_att_aft=world_get_energy(id),
        e_tgt_bef=nil, e_tgt_aft=nil
      })
    end
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
      -- Charged first
      if use_charged_if_ready(u.id) then end

      -- Fast move
      if timers[u.id] <= 0.0 then
        local fastName = unit_fast_move(u.id)
        local m = move_get(fastName)
        local cd = (m.cooldownSec or 0.5)
        timers[u.id] = cd

        local tgt = find_adjacent_enemy(u.id)
        if tgt then
          emit(string.format("%s used %s!", get_name(u.id), string.gsub(fastName, "_", " ")))

          local e_att_bef = world_get_energy(u.id)
          local e_tgt_bef = world_get_energy(tgt)
          local tSnap = world_get_unit_snapshot(tgt)
          local hp_before = tSnap and tSnap.hp or 0

          if rng() < MISS_CHANCE then
            emit("It missed!")
            world_add_energy(u.id, m.energyGain or 0)
            local e_att_aft = world_get_energy(u.id)
            local e_tgt_aft = world_get_energy(tgt)

            log_hit("fast", u.id, fastName, tgt, {
              miss=true, crit=false, dmg=0,
              hp_before=hp_before, hp_after=hp_before,
              e_att_bef=e_att_bef, e_att_aft=e_att_aft,
              e_tgt_bef=e_tgt_bef, e_tgt_aft=e_tgt_aft
            })
          else
            local dmg = m.power or 0
            local crit = false
            if rng() < CRIT_CHANCE then
              dmg = math.floor(dmg * CRIT_MULT + 0.5)
              crit = true
              emit("A critical hit!")
            end

            local rem = world_apply_damage(u.id, tgt, dmg)
            world_add_energy(u.id, m.energyGain or 0)
            world_add_energy(tgt, 8)

            local eff = effectiveness(u.id, tgt); maybe_emit_effectiveness(eff)

            local e_att_aft = world_get_energy(u.id)
            local e_tgt_aft = world_get_energy(tgt)

            log_hit("fast", u.id, fastName, tgt, {
              miss=false, crit=crit, dmg=dmg,
              hp_before=hp_before, hp_after=rem,
              e_att_bef=e_att_bef, e_att_aft=e_att_aft,
              e_tgt_bef=e_tgt_bef, e_tgt_aft=e_tgt_aft
            })

            if rem == 0 then emit(string.format("%s fainted!", get_name(tgt))) end
          end
        end
      end
    end
  end

  -- Passive face nearest enemy
  for i = 1, #units do
    local u = units[i]
    if u.alive then world_face_enemy(u.id) end
  end
end
