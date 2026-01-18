-- scripts/systems/camera.lua
-- Left drag: pan
-- Right drag: orbit around board (camera target)
-- Wheel: zoom

local S = {
  mode = nil, -- "pan" | "orbit" | nil
  lastx = 0, lasty = 0,

  pan_speed = 0.02,     -- world units per pixel
  rot_speed = 0.005,    -- radians per pixel
  zoom_step = 0.5       -- camera.zoom delta per wheel notch
}

-- SDL button ids: 1=left, 2=middle, 3=right
local BTN_LEFT  = 1
local BTN_RIGHT = 3

function camera_init() end
function camera_update(dt) end

function camera_mouse_down(x, y, button)
  S.lastx, S.lasty = x, y

  if button == BTN_LEFT then
    S.mode = "pan"
  elseif button == BTN_RIGHT then
    S.mode = "orbit"
  else
    S.mode = nil
  end
end

function camera_mouse_up(x, y, button)
  S.mode = nil
end

function camera_mouse_move(x, y)
  if not S.mode then return end

  local dx = x - S.lastx
  local dy = y - S.lasty
  S.lastx, S.lasty = x, y

  if S.mode == "pan" then
    -- same pan behavior as before
    cam_move(-dx * S.pan_speed, 0.0, -dy * S.pan_speed)

  elseif S.mode == "orbit" then
    -- orbit around target: horizontal drag = yaw, vertical drag = pitch
    cam_orbit(-dx * S.rot_speed, -dy * S.rot_speed)
  end
end

function camera_mouse_wheel(wheelY)
  cam_zoom(wheelY * S.zoom_step)
end
