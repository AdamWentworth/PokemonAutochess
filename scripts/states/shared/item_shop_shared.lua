-- scripts/states/shared/item_shop_shared.lua

local SHOP_DURATION = 30.0

local ITEM_CATALOG = {
    { id = "pokeball",      label = "Pokeball",      cost = 200, row = 1, col = 4 },
    { id = "potion",        label = "Potion",        cost = 300, row = 2, col = 4 },
    { id = "burn_heal",     label = "Burn Heal",     cost = 300, row = 2, col = 6 },
    { id = "antidote",      label = "Antidote",      cost = 100, row = 2, col = 5 },
    { id = "paralyze_heal", label = "Paralyze Heal", cost = 200, row = 2, col = 9 }
}

local ITEM_IMAGE = "assets/images/items_atlas.png"
local ITEM_ATLAS_COLS = 13
local ITEM_ATLAS_ROWS = 14
local ITEM_ATLAS_PAD_U_FRAC = 0.08
local ITEM_ATLAS_PAD_V_FRAC = 0.08
local ITEM_ATLAS_PAD_U_FRAC_RIGHT = 0.06
local ITEM_ATLAS_PAD_V_FRAC_BOTTOM = 0.06

local function atlas_uv(row, col)
    local c = math.max(1, col or 1)
    local r = math.max(1, row or 1)

    local u0 = (c - 1) / ITEM_ATLAS_COLS
    local u1 = c / ITEM_ATLAS_COLS
    local v0 = (r - 1) / ITEM_ATLAS_ROWS
    local v1 = r / ITEM_ATLAS_ROWS

    u0 = u0 + (ITEM_ATLAS_PAD_U_FRAC / ITEM_ATLAS_COLS)
    v0 = v0 + (ITEM_ATLAS_PAD_V_FRAC / ITEM_ATLAS_ROWS)
    u1 = u1 - (ITEM_ATLAS_PAD_U_FRAC_RIGHT / ITEM_ATLAS_COLS)
    v1 = v1 - (ITEM_ATLAS_PAD_V_FRAC_BOTTOM / ITEM_ATLAS_ROWS)
    return { u0, v0, u1, v1 }
end

local function build_cards()
    local cards = {}
    for _, it in ipairs(ITEM_CATALOG) do
        cards[#cards + 1] = {
            name = it.id,
            cost = it.cost,
            label = it.label .. " $" .. tostring(it.cost),
            image = ITEM_IMAGE,
            uv = atlas_uv(it.row, it.col),
            type = "Item"
        }
    end
    return cards
end

local function make_message(label, time_left)
    local seconds = math.max(0, math.floor(time_left))
    if label and label ~= "" then
        return string.format("%s - Time left: %ds", label, seconds)
    end
    return string.format("Time left: %ds", seconds)
end

local M = {}

function M.install(cfg, target)
    cfg = cfg or {}
    target = target or _ENV

    local message_label = cfg.message_label
    local next_combat_path = cfg.next_combat_path
    if not next_combat_path or next_combat_path == "" then
        error("item_shop_shared.install requires next_combat_path")
    end

    local time_left = SHOP_DURATION
    local transitioned = false

    target.on_enter = function()
        time_left = SHOP_DURATION
        transitioned = false
    end

    target.get_message = function()
        return make_message(message_label, time_left)
    end

    target.get_shop_cards = function()
        return build_cards()
    end

    target.on_shop_card_click = function(item_id)
        if not item_id or item_id == "" then
            return
        end

        for _, it in ipairs(ITEM_CATALOG) do
            if it.id == item_id then
                if spend_money(it.cost) then
                    add_item(item_id, 1)
                    emit("Shop", "Bought " .. it.label)
                else
                    emit("Shop", "Not enough money for " .. it.label)
                end
                return
            end
        end
    end

    target.on_update = function(dt)
        if transitioned then
            return
        end

        time_left = time_left - (dt or 0.016)
        if time_left <= 0.0 then
            transitioned = true
            pop_state()
            push_combat_state(next_combat_path)
        end
    end

    target.on_shop_ready_click = function()
        if transitioned then
            return
        end
        time_left = 0.0
        emit("Shop", "Ready. Starting next round...")
    end
end

return M
