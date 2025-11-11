-- scripts/systems/movement.lua

-- Tunables (mirrors movement_rules.md)
local allow_diagonal = true
local cost_diag = 1.414
local cost_straight = 1.0

-- 8-connected neighborhood
local dirs = {
  {-1,0},{1,0},{0,-1},{0,1},
  {-1,-1},{1,1},{-1,1},{1,-1},
}

local function chebyshev(a,b) return math.max(math.abs(a.col-b.col), math.abs(a.row-b.row)) end
local function manhattan(a,b) return math.abs(a.col-b.col)+math.abs(a.row-b.row) end

local function inside(col,row)
  return col>=0 and col<GRID_COLS and row>=0 and row<GRID_ROWS
end

-- Simple A*: stop when adjacent to target
local function a_star(start, target, blocked)
  local function key(c,r) return (r<<16) | (c & 0xFFFF) end
  local open = {}
  local openSet = {}
  local g = {}
  local came = {}

  local function push(n)
    table.insert(open, n)
    openSet[key(n.col,n.row)] = true
  end
  local function pop()
    local best_i, best_f = 1, open[1].f
    for i=2,#open do
      if open[i].f < best_f then best_i, best_f = i, open[i].f end
    end
    local n = open[best_i]
    table.remove(open, best_i)
    openSet[key(n.col,n.row)] = nil
    return n
  end

  local function heuristic(c,r)
    local dx = c - target.col
    local dy = r - target.row
    return math.sqrt(dx*dx + dy*dy)
  end

  local sK = key(start.col,start.row)
  g[sK] = 0.0
  push{col=start.col,row=start.row,f=heuristic(start.col,start.row)}

  while #open>0 do
    local cur = pop()
    if chebyshev(cur, target) == 1 then
      -- reconstruct
      local path = {{col=cur.col,row=cur.row}}
      local ck = key(cur.col,cur.row)
      while came[ck] do
        local p = came[ck]
        table.insert(path, 1, {col=p.col,row=p.row})
        ck = key(p.col,p.row)
      end
      return path
    end

    for _,d in ipairs(dirs) do
      local nc,nr = cur.col + d[1], cur.row + d[2]
      if inside(nc,nr) and not blocked[key(nc,nr)] then
        -- movement cost
        local diag = (d[1] ~= 0 and d[2] ~= 0)
        local step = diag and cost_diag or cost_straight
        local nk = key(nc,nr)
        local ng = (g[key(cur.col,cur.row)] or math.huge) + step
        if ng < (g[nk] or math.huge) then
          g[nk] = ng
          came[nk] = {col=cur.col,row=cur.row}
          local f = ng + heuristic(nc,nr)
          if not openSet[nk] then push{col=nc,row=nr,f=f} end
        end
      end
    end
  end
  return {} -- no path
end

-- Deterministic sort: distance-to-enemy, then higher speed, then lower id
local function sort_priority(units)
  table.sort(units, function(a,b)
    if a.dist ~= b.dist then return a.dist < b.dist end
    if a.speed ~= b.speed then return a.speed > b.speed end
    return a.id < b.id
  end)
end

-- Build blocked set from reservations (planning-time reservations only)
local function build_blocked(reserved)
  local set = {}
  for k,_ in pairs(reserved) do set[k] = true end
  return set
end

local function k(col,row) return (row<<16) | (col & 0xFFFF) end

function movement_init()
  -- nothing required; kept for symmetry
end

function movement_update(dt)
  -- Snapshot world
  local units_tbl = world_list_units()
  if units_tbl == nil then return end

  -- Prepare enriched list (ignore dead)
  local units = {}
  for i=1,#units_tbl do
    local u = units_tbl[i]
    if u.alive then
      local ec, er = world_nearest_enemy_cell(u.id)
      local dist = math.huge
      if ec ~= -1 then
        dist = math.sqrt((u.col-ec)^2 + (u.row-er)^2)
      end
      table.insert(units, {
        id=u.id, side=u.side, col=u.col, row=u.row, speed=u.speed,
        enemyCol=ec, enemyRow=er, dist=dist
      })
    end
  end

  -- Randomization optional; we stick to deterministic priority:
  sort_priority(units)

  -- 1) Plan: compute desired target cell per unit
  local desired = {}           -- id -> {col,row}
  local reserved = {}          -- gridKey -> id (reservation)
  local triedAlt = {}          -- id -> set(gridKey) to avoid repeat attempts

  for _,u in ipairs(units) do
    -- If engaged, hold position
    if world_is_adjacent_to_enemy(u.id) then
      desired[u.id] = {col=u.col,row=u.row}
      reserved[k(u.col,u.row)] = u.id
    else
      -- Build blocked set from current reservations
      local blocked = build_blocked(reserved)
      -- A* toward adjacency
      local path = {}
      if u.enemyCol ~= -1 then
        path = a_star({col=u.col,row=u.row}, {col=u.enemyCol,row=u.enemyRow}, blocked)
      end
      local primary = (path[2] and {col=path[2].col,row=path[2].row}) or {col=u.col,row=u.row}

      -- Reserve if free; else seek alternate (no swaps, no slide-by)
      local wantKey = k(primary.col, primary.row)
      if reserved[wantKey] == nil then
        desired[u.id] = primary
        reserved[wantKey] = u.id
      else
        -- Alternate search: spiral radius, skip reserved and previously tried,
        -- fallback to hold.
        triedAlt[u.id] = triedAlt[u.id] or {}
        triedAlt[u.id][wantKey] = true

        local chosen = nil
        local maxR = 3
        for r=1,maxR do
          for dx=-r,r do
            for dy=-r,r do
              if math.abs(dx)==r or math.abs(dy)==r then
                local ac, ar = u.col+dx, u.row+dy
                local ak = k(ac,ar)
                if inside(ac,ar)
                   and reserved[ak] == nil
                   and not triedAlt[u.id][ak] then
                  chosen = {col=ac,row=ar}
                  break
                end
              end
            end
            if chosen then break end
          end
          if chosen then break end
        end
        if not chosen then chosen = {col=u.col,row=u.row} end
        desired[u.id] = chosen
        reserved[k(chosen.col,chosen.row)] = u.id
      end
    end
  end

  -- 2) Conflict resolution (deterministic): if multiple desire same cell,
  --    winner is closer-to-enemy, tie: higher speed, fallback lower id.
  local cell2ids = {}
  for id,pos in pairs(desired) do
    local kk = k(pos.col,pos.row)
    cell2ids[kk] = cell2ids[kk] or {}
    table.insert(cell2ids[kk], id)
  end

  local winners = {}  -- id -> true
  for kk, ids in pairs(cell2ids) do
    if #ids == 1 then
      winners[ids[1]] = true
    else
      -- rank candidates by (dist-to-enemy, -speed, +id)
      table.sort(ids, function(a,b)
        local ua, ub
        for _,u in ipairs(units) do
          if u.id==a then ua=u elseif u.id==b then ub=u end
        end
        -- Defensive: if not found, keep stable order
        if not ua or not ub then return a < b end
        -- recompute their "distance to enemy" using snapshot
        local da = ua.dist
        local db = ub.dist
        if da ~= db then return da < db end
        if ua.speed ~= ub.speed then return ua.speed > ub.speed end
        return ua.id < ub.id
      end)
      winners[ids[1]] = true
      -- losers remain stationary (do not animate)
    end
  end

  -- 3) Apply winners only (interpolation is handled by engine’s visual update)
  for _,u in ipairs(units) do
    local pos = desired[u.id]
    if pos and winners[u.id] then
      world_apply_move(u.id, pos.col, pos.row)
    end
  end

  -- 4) Orientation update (face nearest enemy or keep previous)
  for _,u in ipairs(units) do
    world_face_enemy(u.id)
  end
end
