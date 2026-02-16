-- scripts/states/shared/shop_state_shared.lua

local shop_catalog = dofile("scripts/states/shared/shop_catalog.lua")
local mode_utils = dofile("scripts/states/shared/mode_utils.lua")

local function combat_script_path(route_id)
    return "scripts/states/" .. route_id .. ".lua"
end

local M = {}

function M.install(shop_id, mode, target)
    local shop = shop_catalog.get(shop_id)
    if not shop then
        error("Unknown shop id: " .. tostring(shop_id))
    end

    target = target or _ENV

    local next_combat_path = combat_script_path(shop.next_combat_route)
    local resolved_mode = mode_utils.normalize(mode)

    if resolved_mode == "adventure" then
        local adventure_shop = dofile("scripts/states/shared/item_shop_shared.lua")
        adventure_shop.install({
            message_label = shop.message_label,
            next_combat_path = next_combat_path
        }, target)
        return
    end

    local classic_cfg = shop.classic or {}
    local classic_shop = dofile("scripts/states/shared/classic_shop_shared.lua")
    classic_shop.install({
        message_label = shop.message_label,
        commons = classic_cfg.commons or {},
        common_weight = classic_cfg.common_weight or 0.5,
        level_min = classic_cfg.level_min or 1,
        level_max = classic_cfg.level_max or 1,
        show_income_hint = classic_cfg.show_income_hint == true,
        next_combat_path = next_combat_path
    }, target)
end

return M
