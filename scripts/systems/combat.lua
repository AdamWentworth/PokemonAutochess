-- scripts/systems/combat.lua
-- Simple adjacent-melee combat:
-- - Units attack once per cooldown when an enemy is in any of the 8 adjacent cells.
-- - Damage = attacker.attack
-- - Dead units are marked and no longer rendered.

local COOLDOWN = 1.0  -- seconds per attack
local timers = {}     -- id -> time until next attack (<=0 means ready)

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

local function reset_if_missing(id)
  if timers[id] == nil then timers[id] = 0.0 end
end

local function find_adjacent_enemy(id)
  local enemies = world_enemies_adjacent(id) -- returns array of enemy unitIds
  if enemies and #enemies > 0 then
    -- choose lowest HP target to finish kills faster (stable by id on ties)
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

function combat_init()
  timers = {}
end

function combat_update(dt)
  dt = clamp(dt or 0.016, 0.0, 0.25)

  -- Pull immutable snapshot of all units
  local units = world_list_units()
  if not units then return end

  -- Tick cooldowns
  for i=1,#units do
    local u = units[i]
    reset_if_missing(u.id)
    timers[u.id] = math.max(0.0, (timers[u.id] or 0.0) - dt)
  end

  -- Resolve attacks (stateless pass; damage applied via binding)
  for i=1,#units do
    local u = units[i]
    if u.alive then
      -- only attack when engaged (adjacent)
      if world_is_adjacent_to_enemy(u.id) then
        if timers[u.id] <= 0.0 then
          local targetId = find_adjacent_enemy(u.id)
          if targetId then
            world_apply_damage(u.id, targetId, u.attack)
            timers[u.id] = COOLDOWN
          end
        end
      end
    end
  end

  -- Optional orientation polish: face nearest engaged target (cheap)
  for i=1,#units do
    local u = units[i]
    if u.alive then world_face_enemy(u.id) end
  end
end
