-- scripts/states/shared/mode_utils.lua

local M = {}

function M.normalize(mode)
    if not mode and get_game_mode then
        mode = get_game_mode()
    end
    if mode == "adventure" then
        return "adventure"
    end
    return "classic"
end

return M
