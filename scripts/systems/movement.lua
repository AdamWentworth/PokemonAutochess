-- scripts/systems/movement.lua

-- Tunables (mirrors movement_rules.md)
local cost_diag = 1.414
local cost_straight = 1.0

local abs = math.abs
local max = math.max
local min = math.min
local huge = math.huge
local tinsert = table.insert
local tremove = table.remove
local tsort = table.sort

-- 8-connected neighborhood
local dirs = {
  {-1,0},{1,0},{0,-1},{0,1},
  {-1,-1},{1,1},{-1,1},{1,-1},
}

local function chebyshev(a,b)
  return max(abs(a.col - b.col), abs(a.row - b.row))
end

local function inside(col,row)
  return col >= 0 and col < GRID_COLS and row >= 0 and row < GRID_ROWS
end

local function k(col,row)
  return (row << 16) | (col & 0xFFFF)
end

-- Build blocked set from current unit positions + current reservations
local function build_blocked(reserved, blockerKeys)
  local set = {}
  for i = 1, #blockerKeys do
    set[blockerKeys[i]] = true
  end
  for kk,_ in pairs(reserved) do
    set[kk] = true
  end
  return set
end

local function heuristic(c, r, targetCol, targetRow)
  local dx = abs(c - targetCol)
  local dy = abs(r - targetRow)
  local diag = min(dx, dy)
  return cost_diag * diag + cost_straight * (max(dx, dy) - diag)
end

-- Simple A*: stop when adjacent to target
local function a_star(start, target, blocked)
  local open = {}
  local openSet = {}
  local g = {}
  local came = {}

  local function push(n)
    tinsert(open, n)
    openSet[k(n.col, n.row)] = true
  end

  local function pop()
    local best_i, best_f = 1, open[1].f
    for i = 2, #open do
      if open[i].f < best_f then
        best_i, best_f = i, open[i].f
      end
    end
    local n = open[best_i]
    tremove(open, best_i)
    openSet[k(n.col, n.row)] = nil
    return n
  end

  local startKey = k(start.col, start.row)
  g[startKey] = 0.0
  push{
    col = start.col,
    row = start.row,
    f = heuristic(start.col, start.row, target.col, target.row),
  }

  while #open > 0 do
    local cur = pop()
    if chebyshev(cur, target) == 1 then
      local path = {{col = cur.col, row = cur.row}}
      local ck = k(cur.col, cur.row)
      while came[ck] do
        local p = came[ck]
        tinsert(path, 1, {col = p.col, row = p.row})
        ck = k(p.col, p.row)
      end
      return path
    end

    local curKey = k(cur.col, cur.row)
    local curG = g[curKey] or huge
    for _,d in ipairs(dirs) do
      local nc, nr = cur.col + d[1], cur.row + d[2]
      local nextKey = k(nc, nr)
      if inside(nc, nr) and not blocked[nextKey] then
        local diag = (d[1] ~= 0 and d[2] ~= 0)
        local step = diag and cost_diag or cost_straight
        local ng = curG + step
        if ng < (g[nextKey] or huge) then
          g[nextKey] = ng
          came[nextKey] = {col = cur.col, row = cur.row}
          local f = ng + heuristic(nc, nr, target.col, target.row)
          if not openSet[nextKey] then
            push{col = nc, row = nr, f = f}
          end
        end
      end
    end
  end

  return {}
end

-- Deterministic sort: distance-to-enemy, then higher speed, then lower id
local function sort_priority(units)
  tsort(units, function(a,b)
    if a.dist ~= b.dist then return a.dist < b.dist end
    if a.speed ~= b.speed then return a.speed > b.speed end
    return a.id < b.id
  end)
end

function movement_init()
  -- no-op
end

function movement_update(dt)
  local units_tbl = world_list_units_movement()
  if units_tbl == nil then return end

  local units = {}
  local unitById = {}
  local blockerKeys = {}
  local occupantByCell = {}

  for i = 1, #units_tbl do
    local u = units_tbl[i]
    if u.blocksTile then
      local cellKey = k(u.col, u.row)
      blockerKeys[#blockerKeys + 1] = cellKey
      occupantByCell[cellKey] = u.id
    end

    if u.alive then
      local dist = huge
      if u.enemyCol ~= -1 then
        local dx = u.col - u.enemyCol
        local dy = u.row - u.enemyRow
        dist = dx * dx + dy * dy
      end

      local entry = {
        id = u.id,
        col = u.col,
        row = u.row,
        speed = u.speed,
        isMoving = (u.isMoving == true),
        plannedCol = u.plannedCol or -1,
        plannedRow = u.plannedRow or -1,
        enemyCol = u.enemyCol,
        enemyRow = u.enemyRow,
        adjacentToEnemy = (u.adjacentToEnemy == true),
        dist = dist,
      }
      units[#units + 1] = entry
      unitById[entry.id] = entry
    end
  end

  sort_priority(units)

  -- 1) Plan: compute desired target cell per unit
  local desired = {}   -- id -> {col,row}
  local reserved = {}  -- gridKey -> id

  for _,u in ipairs(units) do
    if u.isMoving and u.plannedCol ~= -1 and u.plannedRow ~= -1 then
      local planned = {col = u.plannedCol, row = u.plannedRow}
      desired[u.id] = planned
      reserved[k(planned.col, planned.row)] = u.id
    elseif u.adjacentToEnemy then
      desired[u.id] = {col = u.col, row = u.row}
      reserved[k(u.col, u.row)] = u.id
    else
      local blocked = build_blocked(reserved, blockerKeys)

      local path = {}
      if u.enemyCol ~= -1 then
        path = a_star(
          {col = u.col, row = u.row},
          {col = u.enemyCol, row = u.enemyRow},
          blocked)
      end

      local primary = (path[2] and {col = path[2].col, row = path[2].row}) or
                      {col = u.col, row = u.row}
      local wantKey = k(primary.col, primary.row)
      if reserved[wantKey] == nil then
        desired[u.id] = primary
        reserved[wantKey] = u.id
      else
        desired[u.id] = {col = u.col, row = u.row}
        reserved[k(u.col, u.row)] = u.id
      end
    end
  end

  -- 2) Conflict resolution: same-cell competition
  local cell2ids = {}
  for id,pos in pairs(desired) do
    local kk = k(pos.col, pos.row)
    cell2ids[kk] = cell2ids[kk] or {}
    tinsert(cell2ids[kk], id)
  end

  local winners = {}  -- id -> true
  for _, ids in pairs(cell2ids) do
    if #ids == 1 then
      winners[ids[1]] = true
    else
      tsort(ids, function(a, b)
        local ua = unitById[a]
        local ub = unitById[b]
        if not ua or not ub then return a < b end
        if ua.dist ~= ub.dist then return ua.dist < ub.dist end
        if ua.speed ~= ub.speed then return ua.speed > ub.speed end
        return ua.id < ub.id
      end)
      winners[ids[1]] = true
    end
  end

  -- 2b) Prevent mutual swaps (A wants B's cell and B wants A's cell) -> cancel both
  local wants = {}
  for id,pos in pairs(desired) do
    wants[id] = {col = pos.col, row = pos.row}
  end

  for idA, wa in pairs(wants) do
    if winners[idA] then
      local idB = occupantByCell[k(wa.col, wa.row)]
      if idB and idB ~= idA and winners[idB] then
        local wb = wants[idB]
        local atA = unitById[idA]
        if wb and atA and wb.col == atA.col and wb.row == atA.row then
          winners[idA] = nil
          winners[idB] = nil
        end
      end
    end
  end

  -- 3) Apply winners only (interpolate handled on the C++ side via world_commit_move)
  for _,u in ipairs(units) do
    local pos = desired[u.id]
    if pos and winners[u.id] and (not u.isMoving) then
      world_commit_move(u.id, pos.col, pos.row)
    end
  end

  -- 4) Orientation update
  for _,u in ipairs(units) do
    if u.enemyCol ~= -1 then
      world_face_enemy(u.id, u.enemyCol, u.enemyRow)
    else
      world_face_enemy(u.id)
    end
  end
end
