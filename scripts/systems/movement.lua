-- scripts/systems/movement.lua

-- Tunables (mirrors movement_rules.md)
local cost_diag = 1.414
local cost_straight = 1.0

local abs = math.abs
local max = math.max
local min = math.min
local huge = math.huge
local tsort = table.sort

-- 8-connected neighborhood
local dirs = {
  {-1,0},{1,0},{0,-1},{0,1},
  {-1,-1},{1,1},{-1,1},{1,-1},
}

local astarOpenCols = {}
local astarOpenRows = {}
local astarOpenFs = {}
local astarOpenSet = {}
local astarG = {}
local astarCameCols = {}
local astarCameRows = {}

local function inside(col,row)
  return col >= 0 and col < GRID_COLS and row >= 0 and row < GRID_ROWS
end

local function cell_index(col, row)
  return row * GRID_COLS + col + 1
end

local function heuristic(c, r, targetCol, targetRow)
  local dx = abs(c - targetCol)
  local dy = abs(r - targetRow)
  local diag = min(dx, dy)
  return cost_diag * diag + cost_straight * (max(dx, dy) - diag)
end

local function better_priority(unitA, unitB)
  if unitA.dist ~= unitB.dist then return unitA.dist < unitB.dist end
  if unitA.speed ~= unitB.speed then return unitA.speed > unitB.speed end
  return unitA.id < unitB.id
end

-- Simple A*: stop when adjacent to target and return only the first step.
local function a_star_first_step(startCol, startRow, targetCol, targetRow, blocked)
  local totalCells = GRID_COLS * GRID_ROWS
  for i = 1, totalCells do
    astarOpenSet[i] = false
    astarG[i] = nil
    astarCameCols[i] = nil
    astarCameRows[i] = nil
  end

  local openCount = 1
  local startCell = cell_index(startCol, startRow)
  astarOpenCols[1] = startCol
  astarOpenRows[1] = startRow
  astarOpenFs[1] = heuristic(startCol, startRow, targetCol, targetRow)
  astarOpenSet[startCell] = true
  astarG[startCell] = 0.0

  while openCount > 0 do
    local best_i = 1
    local best_f = astarOpenFs[1]
    for i = 2, openCount do
      local fi = astarOpenFs[i]
      if fi < best_f then
        best_i = i
        best_f = fi
      end
    end

    local curCol = astarOpenCols[best_i]
    local curRow = astarOpenRows[best_i]
    local curCell = cell_index(curCol, curRow)

    astarOpenSet[curCell] = false
    if best_i ~= openCount then
      astarOpenCols[best_i] = astarOpenCols[openCount]
      astarOpenRows[best_i] = astarOpenRows[openCount]
      astarOpenFs[best_i] = astarOpenFs[openCount]
    end
    astarOpenCols[openCount] = nil
    astarOpenRows[openCount] = nil
    astarOpenFs[openCount] = nil
    openCount = openCount - 1

    if max(abs(curCol - targetCol), abs(curRow - targetRow)) == 1 then
      local stepCol = curCol
      local stepRow = curRow
      local parentCol = astarCameCols[curCell]
      local parentRow = astarCameRows[curCell]
      while parentCol ~= nil and parentRow ~= nil do
        local parentCell = cell_index(parentCol, parentRow)
        local grandCol = astarCameCols[parentCell]
        local grandRow = astarCameRows[parentCell]
        if grandCol == nil or grandRow == nil then
          return stepCol, stepRow
        end
        stepCol = parentCol
        stepRow = parentRow
        parentCol = grandCol
        parentRow = grandRow
      end
      return nil, nil
    end

    local curG = astarG[curCell] or huge
    for _, d in ipairs(dirs) do
      local nc = curCol + d[1]
      local nr = curRow + d[2]
      if inside(nc, nr) then
        local nextCell = cell_index(nc, nr)
        if not blocked[nextCell] then
          local diag = (d[1] ~= 0 and d[2] ~= 0)
          local step = diag and cost_diag or cost_straight
          local ng = curG + step
          if ng < (astarG[nextCell] or huge) then
            astarG[nextCell] = ng
            astarCameCols[nextCell] = curCol
            astarCameRows[nextCell] = curRow
            if not astarOpenSet[nextCell] then
              openCount = openCount + 1
              astarOpenCols[openCount] = nc
              astarOpenRows[openCount] = nr
              astarOpenFs[openCount] = ng + heuristic(nc, nr, targetCol, targetRow)
              astarOpenSet[nextCell] = true
            end
          end
        end
      end
    end
  end

  return nil, nil
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
  local occupantByCell = {}
  local blocked = {}

  for i = 1, #units_tbl do
    local u = units_tbl[i]
    if u.blocksTile then
      local cellKey = cell_index(u.col, u.row)
      blocked[cellKey] = true
      occupantByCell[cellKey] = u.id
    end

    if u.alive then
      local dist = huge
      if u.enemyCol ~= -1 then
        local dx = u.col - u.enemyCol
        local dy = u.row - u.enemyRow
        dist = dx * dx + dy * dy
      end

      u.isMoving = (u.isMoving == true)
      u.plannedCol = u.plannedCol or -1
      u.plannedRow = u.plannedRow or -1
      u.adjacentToEnemy = (u.adjacentToEnemy == true)
      u.dist = dist
      units[#units + 1] = u
      unitById[u.id] = u
    end
  end

  sort_priority(units)

  -- 1) Plan: compute desired target cell per unit
  local desiredCols = {}
  local desiredRows = {}
  local desiredCells = {}
  local claimed = {}

  for _,u in ipairs(units) do
    if u.isMoving and u.plannedCol ~= -1 and u.plannedRow ~= -1 then
      local plannedCell = cell_index(u.plannedCol, u.plannedRow)
      desiredCols[u.id] = u.plannedCol
      desiredRows[u.id] = u.plannedRow
      desiredCells[u.id] = plannedCell
      claimed[plannedCell] = u.id
      blocked[plannedCell] = true
    elseif u.adjacentToEnemy then
      local stayCell = cell_index(u.col, u.row)
      desiredCols[u.id] = u.col
      desiredRows[u.id] = u.row
      desiredCells[u.id] = stayCell
      claimed[stayCell] = u.id
      blocked[stayCell] = true
    else
      local nextCol, nextRow = nil, nil
      if u.enemyCol ~= -1 then
        nextCol, nextRow = a_star_first_step(
          u.col,
          u.row,
          u.enemyCol,
          u.enemyRow,
          blocked)
      end

      local wantCol = nextCol or u.col
      local wantRow = nextRow or u.row
      local wantCell = cell_index(wantCol, wantRow)
      if claimed[wantCell] == nil then
        desiredCols[u.id] = wantCol
        desiredRows[u.id] = wantRow
        desiredCells[u.id] = wantCell
        claimed[wantCell] = u.id
        blocked[wantCell] = true
      else
        local stayCell = cell_index(u.col, u.row)
        desiredCols[u.id] = u.col
        desiredRows[u.id] = u.row
        desiredCells[u.id] = stayCell
        claimed[stayCell] = u.id
        blocked[stayCell] = true
      end
    end
  end

  -- 2) Conflict resolution: same-cell competition
  local cellWinners = {}
  local winners = {}  -- id -> true
  for _, u in ipairs(units) do
    local id = u.id
    local wantCell = desiredCells[id]
    if wantCell ~= nil then
      local incumbent = cellWinners[wantCell]
      if incumbent == nil or better_priority(unitById[id], unitById[incumbent]) then
        cellWinners[wantCell] = id
      end
    end
  end
  for _, id in pairs(cellWinners) do
    winners[id] = true
  end

  -- 2b) Prevent mutual swaps (A wants B's cell and B wants A's cell) -> cancel both
  for idA, wantCell in pairs(desiredCells) do
    if winners[idA] then
      local idB = occupantByCell[wantCell]
      if idB and idB ~= idA and winners[idB] then
        local wbCol = desiredCols[idB]
        local wbRow = desiredRows[idB]
        local atA = unitById[idA]
        if wbCol ~= nil and wbRow ~= nil and atA and wbCol == atA.col and wbRow == atA.row then
          winners[idA] = nil
          winners[idB] = nil
        end
      end
    end
  end

  -- 3) Apply winners only (interpolate handled on the C++ side via world_commit_move)
  for _,u in ipairs(units) do
    local wantCol = desiredCols[u.id]
    local wantRow = desiredRows[u.id]
    if wantCol ~= nil and wantRow ~= nil and winners[u.id] and (not u.isMoving) then
      world_commit_move(u.id, wantCol, wantRow)
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
