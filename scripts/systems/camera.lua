-- scripts/systems/camera.lua
-- Pan with RMB drag; zoom with wheel. Adjust numbers here without rebuilding.

local S = {
  panning = false,
  lastx = 0, lasty = 0,
  pan_speed = 0.02,   -- world units per pixel
  zoom_step = 0.5     -- camera.zoom delta per wheel notch
}

function camera_init() end
function camera_update(dt) end

function camera_mouse_down(x, y)
  S.panning = true
  S.lastx, S.lasty = x, y
end

function camera_mouse_up(x, y)
  S.panning = false
end

function camera_mouse_move(x, y)
  if not S.panning then return end
  local dx = x - S.lastx
  local dy = y - S.lasty
  S.lastx, S.lasty = x, y
  cam_move(-dx * S.pan_speed, 0.0, -dy * S.pan_speed)
end

function camera_mouse_wheel(wheelY)
  cam_zoom(wheelY * S.zoom_step)
end
